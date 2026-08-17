/* UFSD#INI.C - UFS Disk Initialization
**
** AP-1d Step 2: Open BDAM datasets at STC startup.
** AP-1f: Dynamic mount/unmount via ufsd_disk_mount_dyn/ufsd_disk_umount.
** AP-3a: Parmlib-driven config, DYNALLOC via SVC 99/__dsfree,
**        root disk auto-create, mount-point directory creation.
**
** ufsd_ufs_init(stc)           read parmlib, mount root + all mounts
** ufsd_ufs_term(stc)           close all disks, DYNFREE allocated DDs
** ufsd_disk_mount_dyn(stc,...) open via DYNALLOC (AP-3a)
** ufsd_disk_umount(stc,path)   close and remove one disk at runtime
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <clibwto.h>
#include <clibio.h>
#include "time64.h"
#include <osio.h>
#include <osdcb.h>
#include <osjfcb.h>
#include <mvssupa.h>
#include "svc99.h"

/* Boot block header (8 bytes at sector 0 offset 0).
** Matches struct ufs_boot in ufs370/include/ufs/disk.h. */
typedef struct ufsboot_hdr UFSBOOT_HDR;
struct ufsboot_hdr {
    unsigned short  type;       /* 00 UFS_DISK_TYPE_UFS = 2             */
    unsigned short  check;      /* 02 ~type: type+check must == 0xFFFF  */
    unsigned short  blksize;    /* 04 physical block size               */
    unsigned short  pad;        /* 06 reserved                          */
};                              /* 08                                   */
#define UFSD_DISK_TYPE_UFS  2

/* The DD name sequence counter lives in UFSD_STC (stc->ddn_seq), not in a
** C static: with AC(1) from an APF-authorized library the module is fetched
** into key-0 storage, where a key-8 store abends S0C4 (#64). */

/* Forward declarations */
static UFSD_DISK *open_disk(const char *ddname);
static void       close_disk(UFSD_DISK *disk);
static int        mkdir_p(UFSD_DISK *disk, const char *path);
static int        find_parent_disk(UFSD_STC *stc, const char *path);

/* ============================================================
** s99_errmsg
**
** Return a short human-readable string for common S99ERROR
** codes.  Returns NULL for unknown codes.
** ============================================================ */
static const char *
s99_errmsg(unsigned short code)
{
    switch (code) {
    case 0x0210U: return "Dataset in use";
    case 0x0218U: return "Dataset allocated exclusively";
    case 0x1708U: return "Dataset not cataloged";
    case 0x170CU: return "Volume not mounted";
    default:      return NULL;
    }
}

/* ============================================================
** ufsd_dynalloc
**
** Allocate a dataset via SVC 99 (DYNALLOC).
** Builds text units directly instead of going through __dsalc,
** so we can capture S99ERROR on failure and issue a clear
** operator message.
**
** ddname  - pre-generated DD name (8 chars, blank-padded)
** dsname  - dataset name
** mode    - UFSD_MOUNT_RW (DISP=OLD) or UFSD_MOUNT_RO (DISP=SHR)
**
** Returns 0 on success, 8 on failure (with WTO issued).
** ============================================================ */
static int
ufsd_dynalloc(const char *ddname, const char *dsname, unsigned mode)
{
    TXT99    **txt  = NULL;
    RB99       rb;
    int        rc;
    const char *msg;

    if (__txddn(&txt, ddname))  goto bad_setup;
    if (__txdsn(&txt, dsname))  goto bad_setup;
    if (mode == UFSD_MOUNT_RW) {
        if (__txold(&txt, NULL))  goto bad_setup;
    } else {
        if (__txshr(&txt, NULL))  goto bad_setup;
    }

    /* Mark end of text unit pointer list */
    {
        unsigned count = 0;
        while (txt[count]) count++;
        if (count == 0) goto bad_setup;
        count--;
        txt[count] = (TXT99 *)((unsigned)txt[count] | 0x80000000U);
    }

    memset(&rb, 0, sizeof(rb));
    rb.len     = (unsigned char)sizeof(RB99);
    rb.request = S99VRBAL;
    rb.flag1   = S99NOCNV;
    rb.txtptr  = txt;

    rc = __svc99(&rb);

    if (rc != 0) {
        msg = s99_errmsg((unsigned short)rb.error);
        if (msg)
            wtof("UFSD120E DYNALLOC FAILED FOR DSN=%s "
                 "(S99ERR=%04X: %s)", dsname,
                 (unsigned)rb.error, msg);
        else
            wtof("UFSD120E DYNALLOC FAILED FOR DSN=%s "
                 "(S99ERR=%04X)", dsname,
                 (unsigned)rb.error);

        FreeTXT99Array(&txt);
        return 8;
    }

    FreeTXT99Array(&txt);
    return 0;

bad_setup:
    wtof("UFSD120E DYNALLOC FAILED FOR DSN=%s "
         "(TEXT UNIT BUILD ERROR)", dsname);
    if (txt) FreeTXT99Array(&txt);
    return 8;
}

/* ============================================================
** open_disk
**
** Allocate a UFSD_DISK handle, open the BDAM dataset, read the
** DSN and disposition from the JFCB, then read and validate the
** boot block to confirm it is a formatted UFS disk.
** Returns a pointer on success, NULL on failure.
** ============================================================ */
static UFSD_DISK *
open_disk(const char *ddname)
{
    UFSD_DISK    *disk;
    DCB          *dcb;
    JFCB          jfcb;
    char         *buf;
    UFSBOOT_HDR  *boot;
    DECB          decb;
    int           i;

    disk = (UFSD_DISK *)calloc(1, sizeof(UFSD_DISK));
    if (!disk) {
        wtof("UFSD042E CANNOT ALLOCATE DISK HANDLE FOR %s", ddname);
        return NULL;
    }

    /* Store DD name (8 chars + NUL) */
    memcpy(disk->ddname, ddname, 8);
    disk->ddname[8] = '\0';

    /* Allocate BDAM DCB */
    dcb = osddcb(disk->ddname);
    if (!dcb) {
        wtof("UFSD042E CANNOT ALLOCATE DCB FOR %s", disk->ddname);
        free(disk);
        return NULL;
    }
    disk->dcb = (void *)dcb;

    /* Install SYNAD exit to suppress IEC020I on I/O errors.
    ** The stub is a BR 14 emitted inline with a branch around it
    ** so it never executes during normal flow but stays in-CSECT. */
    {
        void *synad;
        __asm__("B\tUFSSYNE\n\t"
            "DS\t0H\n"
            "UFSSYND\tDS\t0H\n\t"
            "BR\t14\n"
            "UFSSYNE\tDS\t0H\n\t"
            "LA\t%0,UFSSYND" : "=r"(synad));
        dcb->dcbsynad = synad;
    }

    /* Open for BDAM UPDATE access (read + write) */
    if (osdopen(dcb, 0)) {
        wtof("UFSD043E CANNOT OPEN %s", disk->ddname);
        free(dcb);
        free(disk);
        return NULL;
    }
    disk->flags |= UFSD_DISK_OPEN;

    /* Read DSN and allocation flags from JFCB */
    memset(&jfcb, 0, sizeof(jfcb));
    __rdjfcb(dcb, &jfcb);
    for (i = 0; i < 44 && jfcb.jfcbdsnm[i] > ' '; i++)
        disk->dsn[i] = jfcb.jfcbdsnm[i];
    disk->dsn[i] = '\0';

    if (jfcb.jfcbind2 & JFCSHARE)
        disk->flags |= UFSD_DISK_RDONLY;

    /* Get physical block size from DCB (set by OPEN from the DSCB) */
    disk->blksize = dcb->dcbblksi;
    if (disk->blksize == 0)
        disk->blksize = 4096;       /* safe fallback */

    /* Read and validate boot block (sector 0).
    ** Without the buffer the magic check cannot run, so refuse the mount:
    ** returning the handle anyway would mount an unverified dataset and
    ** then write UFS metadata into it. */
    buf = (char *)calloc(1, (unsigned)disk->blksize);
    if (!buf) {
        wtof("UFSD049E %s: NO STORAGE FOR BOOT BLOCK (%u BYTES)",
             disk->ddname, (unsigned)disk->blksize);
        close_disk(disk);
        return NULL;
    }

    memset(&decb, 0, sizeof(decb));
    osdread(&decb, dcb, buf, (int)disk->blksize, 0);
    oscheck(&decb);

    boot = (UFSBOOT_HDR *)buf;
    if (boot->type != (unsigned short)UFSD_DISK_TYPE_UFS ||
        (unsigned)(boot->type + boot->check) != 0xFFFFU) {
        wtof("UFSD044E %s: NOT A VALID UFS DISK (TYPE=%04X)",
             disk->ddname, (unsigned)boot->type);
        free(buf);
        close_disk(disk);
        return NULL;
    }
    free(buf);

    return disk;
}

/* ============================================================
** close_disk
**
** Close the BDAM dataset (which also frees the DCB storage)
** and release the UFSD_DISK handle.
** ============================================================ */
static void
close_disk(UFSD_DISK *disk)
{
    if (!disk) return;

    if (disk->dcb) {
        if (disk->flags & UFSD_DISK_OPEN)
            osdclose((DCB *)disk->dcb, 1); /* 1 = free DCB storage */
        else
            free(disk->dcb);
        disk->dcb = NULL;
    }
    disk->flags = 0;
    free(disk);
}

/* ============================================================
** mkdir_p
**
** Create all directories along a path on the given disk.
** Like "mkdir -p /a/b/c" — creates /a, /a/b, /a/b/c.
** Silently succeeds if directories already exist.
** Returns UFSD_RC_OK or an error code.
** ============================================================ */
static int
mkdir_p(UFSD_DISK *disk, const char *path)
{
    char         comp[128];
    char         partial[128];
    char         pown[9];      /* parent dir owner, inherited */
    char         pgrp[9];      /* parent dir group, inherited */
    const char  *p;
    const char  *end;
    unsigned     cur_ino;
    unsigned     found;
    unsigned     new_ino;
    unsigned     new_blk;
    int          n;
    int          rc;
    UFSD_DINODE  dino;
    UFSD_DIRENT *de;
    char        *blk;

    if (!path || path[0] != '/') return UFSD_RC_INVALID;

    cur_ino    = UFSD_ROOT_INO;
    partial[0] = '\0';
    p          = path + 1;  /* skip leading '/' */

    while (*p) {
        /* Skip slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Extract next component */
        end = p;
        while (*end && *end != '/') end++;
        n = (int)(end - p);
        if (n == 0) break;
        if (n > (int)UFSD_NAME_MAX) return UFSD_RC_NAMETOOLONG;

        memcpy(comp, p, (unsigned)n);
        comp[n] = '\0';
        p = end;

        /* Build partial path for diagnostics */
        strcat(partial, "/");
        strcat(partial, comp);

        /* Check if this component exists */
        found = ufsd_dir_lookup(disk, cur_ino, comp);
        if (found != 0) {
            /* Existing directory: left exactly as it is.  An earlier
            ** version stamped UFSD/SYS1 and fresh timestamps onto any
            ** directory whose owner field was empty, which made an
            ** empty owner unrepresentable -- every startup wrote the
            ** invented value straight back (issue #52).  An empty
            ** owner is now a value in its own right: unowned. */
            cur_ino = found;
            continue;
        }

        /* Inherit owner/group from the parent directory.  A mount
        ** point is an ordinary directory on the parent filesystem;
        ** inventing an owner for it is what made `ls /u` and
        ** `ufs_stat /u/user` name different owners (issue #52).
        ** Read into dino, which the create path below overwrites. */
        pown[0] = '\0';
        pgrp[0] = '\0';
        if (ufsd_ino_read(disk, cur_ino, &dino) == UFSD_RC_OK) {
            memcpy(pown, dino.owner, sizeof(dino.owner));
            memcpy(pgrp, dino.group, sizeof(dino.group));
        }

        /* Create the directory */
        if (ufsd_sb_alloc_inode(disk, &new_ino) != UFSD_RC_OK)
            return UFSD_RC_NOINODES;
        if (ufsd_sb_alloc_block(disk, &new_blk) != UFSD_RC_OK) {
            ufsd_sb_free_inode(disk, new_ino);
            return UFSD_RC_NOSPACE;
        }

        blk = (char *)calloc(1U, disk->blksize);
        if (!blk) {
            ufsd_sb_free_block(disk, new_blk);
            ufsd_sb_free_inode(disk, new_ino);
            return UFSD_RC_IO;
        }

        de       = (UFSD_DIRENT *)blk;
        de->ino  = new_ino;
        memset(de->name, 0, sizeof(de->name));
        memcpy(de->name, ".", 2);

        de++;
        de->ino  = cur_ino;
        memset(de->name, 0, sizeof(de->name));
        memcpy(de->name, "..", 3);

        rc = ufsd_blk_write(disk, new_blk, blk);
        free(blk);
        if (rc != UFSD_RC_OK) {
            ufsd_sb_free_block(disk, new_blk);
            ufsd_sb_free_inode(disk, new_ino);
            return UFSD_RC_IO;
        }

        memset(&dino, 0, sizeof(dino));
        dino.mode     = (unsigned short)(UFSD_IFDIR | 0755U);
        dino.nlink    = 2;
        dino.filesize = 2U * UFSD_DIRENT_SIZE;
        dino.addr[0]  = new_blk;
        {
            mtime64_t now;
            mtime64(&now);
            dino.ctime.v2 = now;
            dino.mtime.v2 = now;
            dino.atime.v2 = now;
        }
        memcpy(dino.owner, pown, sizeof(dino.owner));
        memcpy(dino.group, pgrp, sizeof(dino.group));

        if (ufsd_ino_write(disk, new_ino, &dino) != UFSD_RC_OK) {
            ufsd_sb_free_block(disk, new_blk);
            ufsd_sb_free_inode(disk, new_ino);
            return UFSD_RC_IO;
        }

        rc = ufsd_dir_add(disk, cur_ino, comp, new_ino);
        if (rc != UFSD_RC_OK) {
            ufsd_sb_free_block(disk, new_blk);
            ufsd_sb_free_inode(disk, new_ino);
            return rc;
        }

        if (ufsd_sb_write(disk) != UFSD_RC_OK)
            return UFSD_RC_IO;

        cur_ino = new_ino;
    }

    return UFSD_RC_OK;
}

/* ============================================================
** ufsd_disk_mount_dyn
**
** AP-3a: Mount a BDAM dataset via DYNALLOC (SVC 99).
** Generates a DD name, allocates the dataset, opens it,
** reads the superblock, and appends to stc->disks[].
**
** Returns 0 on success, 8 on failure.
** ============================================================ */
int
ufsd_disk_mount_dyn(UFSD_STC *stc, const char *dsname,
                    const char *mountpath, unsigned mode,
                    const char *owner)
{
    UFSD_DISK *disk;
    char       ddname[9];
    unsigned   pathlen;
    unsigned   i;

    if (!stc || !dsname || !mountpath) return 8;

    if (mountpath[0] != '/') {
        wtof("UFSD065E MOUNT: PATH MUST BE ABSOLUTE");
        return 8;
    }
    if (stc->ndisks >= (unsigned)UFSD_MAX_DISKS) {
        wtof("UFSD061E MOUNT: DISK TABLE FULL (%u SLOTS)",
             (unsigned)UFSD_MAX_DISKS);
        return 8;
    }

    /* Check for duplicate mount path */
    for (i = 0; i < stc->ndisks; i++) {
        if (stc->disks[i] &&
            strcmp(stc->disks[i]->mountpath, mountpath) == 0) {
            wtof("UFSD067E MOUNT: %s ALREADY MOUNTED", mountpath);
            return 8;
        }
    }

    /* Generate DD name: UFD00001, UFD00002, ... */
    sprintf(ddname, "UFD%05u", ++stc->ddn_seq);

    /* DYNALLOC: DISP=OLD for RW (exclusive), DISP=SHR for RO */
    if (ufsd_dynalloc(ddname, dsname, mode) != 0)
        return 8;

    /* Open BDAM dataset */
    disk = open_disk(ddname);
    if (!disk) {
        __dsfree(ddname);
        return 8;
    }

    /* Read and validate superblock */
    if (ufsd_sb_read(disk) != UFSD_RC_OK) {
        wtof("UFSD124E SUPERBLOCK READ/VALIDATION FAILED FOR DSN=%s",
             disk->dsn);
        close_disk(disk);
        __dsfree(ddname);
        return 8;
    }

    /* Store mount metadata */
    pathlen = strlen(mountpath);
    if (pathlen >= sizeof(disk->mountpath))
        pathlen = sizeof(disk->mountpath) - 1U;
    memcpy(disk->mountpath, mountpath, pathlen);
    disk->mountpath[pathlen] = '\0';

    disk->mount_mode = mode;
    memset(disk->mount_owner, 0, sizeof(disk->mount_owner));
    if (owner && owner[0]) {
        strncpy(disk->mount_owner, owner, 8);
        disk->mount_owner[8] = '\0';
    }

    if (mode == UFSD_MOUNT_RO)
        disk->flags |= UFSD_DISK_RDONLY;

    /* Record where this filesystem hangs in the tree (issue #52).
    ** Resolved once, here, while the parent is still the longest
    ** matching mount: do_dirread reads the table once per directory
    ** entry and must not do path work there.  Done before the disk
    ** joins disks[], or find_parent_disk would match it against its
    ** own mount path and make it its own parent.
    **
    ** This write and the ndisks++ below are what keeps the table
    ** valid: every slot below ndisks has been filled here, so nothing
    ** ever reads the zero the STC was memset to -- which would read
    ** as pdisk 0, the root disk, not as "no parent".  Anything added
    ** between the two must not leave that gap. */
    {
        UFSD_MOUNTPT *mp = &stc->mountpt[stc->ndisks];

        mp->pdisk = UFSD_MNT_NOPARENT;
        mp->ino   = 0U;
        mp->pino  = 0U;

        if (strcmp(disk->mountpath, "/") != 0) {
            int         pidx    = find_parent_disk(stc, disk->mountpath);
            UFSD_DISK  *ppdisk  = stc->disks[pidx];
            const char *rel     = disk->mountpath;
            unsigned    mp_ino;
            unsigned    mp_pino = 0U;
            char        leaf[UFSD_NAME_MAX + 1];

            /* Strip the parent's own mount prefix: a nested mount
            ** point is looked up on the parent disk, whose root is
            ** not "/". */
            if (pidx > 0)
                rel = disk->mountpath + strlen(ppdisk->mountpath);

            mp_ino = rel[0]
                   ? ufsd_path_lookup(ppdisk, UFSD_ROOT_INO, rel,
                                      &mp_pino, leaf)
                   : 0U;

            if (mp_ino != 0U) {
                mp->pdisk = pidx;
                mp->ino   = mp_ino;
                mp->pino  = mp_pino;
            } else {
                /* Reachable by path either way -- only the metadata
                ** crossing in do_dirread is lost. */
                wtof("UFSD126W MOUNT POINT %s NOT FOUND ON PARENT DISK",
                     disk->mountpath);
            }
        }
    }

    /* The disk was formatted for one userid and mounted for another.
    ** Both stay as they are: OWNER() decides who may write, the inode
    ** owner is metadata, and only the operator can say which of the
    ** two is the mistake (issue #52). */
    if (disk->mount_owner[0]) {
        UFSD_DINODE rdino;

        if (ufsd_ino_read(disk, UFSD_ROOT_INO, &rdino) == UFSD_RC_OK
            && rdino.owner[0]
            && strncmp(rdino.owner, disk->mount_owner, 8) != 0)
            wtof("UFSD125W %s ROOT OWNER=%.8s, MOUNTED OWNER=%.8s",
                 disk->dsn, rdino.owner, disk->mount_owner);
    }

    stc->disks[stc->ndisks++] = disk;

    /* Report the root as (ROOT), not (RW).  Root is mounted RW purely so
    ** ufsd_ufs_init can create the mount-point directories, and is flipped
    ** to RO before any client can reach it -- printing RW here would state
    ** the opposite of what clients see.  The test is on the mount path,
    ** not on UFSD_DISK_ROOT: that flag is set by our caller, after this
    ** WTO.  Only '/' can be the root, and a second mount on '/' is already
    ** rejected as a duplicate above. */
    {
        const char *modestr;

        if (strcmp(disk->mountpath, "/") == 0)
            modestr = "ROOT";
        else
            modestr = (mode == UFSD_MOUNT_RW) ? "RW" : "RO";

        if (disk->mount_owner[0])
            wtof("UFSD060I MOUNTED %s ON %s (%s, OWNER=%s)",
                 disk->dsn, disk->mountpath, modestr, disk->mount_owner);
        else
            wtof("UFSD060I MOUNTED %s ON %s (%s)",
                 disk->dsn, disk->mountpath, modestr);
    }

    return 0;
}

/* ============================================================
** path_depth
**
** Count '/' characters in a path.  Used as sort key so that
** shallower mounts are processed before deeper ones, ensuring
** parent filesystems are mounted before their children.
** ============================================================ */
static unsigned
path_depth(const char *p)
{
    unsigned d = 0;
    while (*p) { if (*p == '/') d++; p++; }
    return d;
}

/* ============================================================
** find_parent_disk
**
** Longest-prefix match over already-mounted disks.  Returns
** the disk index (0 = root) whose mountpath is the longest
** prefix of the given path.  Used to create a nested mount's
** mountpoint on the correct parent filesystem instead of the
** root disk.
** ============================================================ */
static int
find_parent_disk(UFSD_STC *stc, const char *path)
{
    unsigned i;
    int      best_idx = 0;
    unsigned best_len = 1;  /* "/" always matches */
    unsigned mlen;

    for (i = 1; i < stc->ndisks; i++) {
        UFSD_DISK *d = stc->disks[i];
        if (!d || d->mountpath[0] == '\0') continue;
        mlen = strlen(d->mountpath);
        if (mlen <= best_len) continue;
        if (strncmp(path, d->mountpath, mlen) == 0
            && (path[mlen] == '/' || path[mlen] == '\0')) {
            best_idx = (int)i;
            best_len = mlen;
        }
    }
    return best_idx;
}

/* ============================================================
** ufsd_ufs_init
**
** AP-3a: Read parmlib configuration, mount root filesystem
** (must already exist; not auto-created), create mount-point directories,
** then mount all configured filesystems.
**
** Parmlib (DD:UFSDPRM) is required.  Returns 8 if missing.
** ============================================================ */
int
ufsd_ufs_init(UFSD_STC *stc)
{
    UFSD_CONFIG cfg;
    UFSD_DISK  *root;
    unsigned    i;
    int         rc;

    if (!stc) return 8;
    stc->ndisks = 0;

    rc = ufsd_cfg_read(&cfg);
    if (rc != 0) {
        wtof("UFSD061E PARMLIB (DD:UFSDPRM) NOT FOUND -- SHUTTING DOWN");
        return 8;
    }

    /* Mount root filesystem via DYNALLOC */
    rc = ufsd_disk_mount_dyn(stc, cfg.root_dsname, "/",
                             UFSD_MOUNT_RW, "");
    if (rc != 0) {
        wtof("UFSD061E CANNOT MOUNT ROOT FILESYSTEM -- SHUTTING DOWN");
        return 8;
    }

    /* Mark as root */
    root = stc->disks[0];
    root->flags |= UFSD_DISK_ROOT;

    /* Sort cfg.mounts[] by path depth (stable insertion sort) so
    ** parent filesystems are mounted before nested children. */
    for (i = 1; i < cfg.nmounts; i++) {
        UFSD_MOUNT_CFG tmp = cfg.mounts[i];
        unsigned       d   = path_depth(tmp.path);
        int            j   = (int)i - 1;
        while (j >= 0 && path_depth(cfg.mounts[j].path) > d) {
            cfg.mounts[j + 1] = cfg.mounts[j];
            j--;
        }
        cfg.mounts[j + 1] = tmp;
    }

    /* Create mountpoint on the correct parent disk, then mount */
    for (i = 0; i < cfg.nmounts; i++) {
        UFSD_MOUNT_CFG *m      = &cfg.mounts[i];
        int             pidx   = find_parent_disk(stc, m->path);
        UFSD_DISK      *pdisk  = stc->disks[pidx];
        const char     *relpath = m->path;
        unsigned        saved_mode;

        /* Strip parent mount prefix to get disk-relative path */
        if (pidx > 0) {
            unsigned mlen = strlen(pdisk->mountpath);
            relpath = m->path + mlen;
        }

        /* Temporarily allow writes on parent (may be RO, e.g. root) */
        saved_mode = pdisk->mount_mode;
        pdisk->mount_mode = UFSD_MOUNT_RW;

        rc = mkdir_p(pdisk, relpath[0] ? relpath : "/");

        pdisk->mount_mode = saved_mode;

        if (rc != UFSD_RC_OK && rc != UFSD_RC_EXIST)
            wtof("UFSD122W CANNOT CREATE MOUNT POINT %s (RC=%d)",
                 m->path, rc);

        /* Mount the child filesystem */
        rc = ufsd_disk_mount_dyn(stc, m->dsname, m->path,
                                 m->mode, m->owner);
        if (rc != 0)
            wtof("UFSD123W CANNOT MOUNT DSN=%s ON %s",
                 m->dsname, m->path);
    }

    /* Root is now RO for clients — all mountpoints are created */
    root->mount_mode = UFSD_MOUNT_RO;

    /* Count only.  Each disk already announced itself with UFSD060I as it
    ** was mounted, so a second pass over the same list would just repeat
    ** it; /F UFSD,MOUNT LIST prints the live state (path, DSN, mode,
    ** owner) on demand. */
    wtof("UFSD040I %u FILESYSTEM(S) MOUNTED", stc->ndisks);

    return 0;
}

/* ============================================================
** ufsd_ufs_term
**
** Close all open BDAM datasets and free disk handles.
** DYNFREE any DD names generated by ufsd_disk_mount_dyn.
** Called at STC shutdown before the session table is freed.
** ============================================================ */
void
ufsd_ufs_term(UFSD_STC *stc)
{
    unsigned i;
    char     ddname[9];

    if (!stc) return;

    for (i = 0; i < stc->ndisks; i++) {
        UFSD_DISK *d = stc->disks[i];
        if (!d) continue;
        /* Write superblock back to disk for RW filesystems */
        if ((d->flags & UFSD_DISK_OPEN) &&
            !(d->flags & UFSD_DISK_RDONLY)) {
            if (ufsd_sb_write(d))
                wtof("UFSD130W SUPERBLOCK WRITEBACK FAILED FOR DSN=%s",
                     d->dsn);
            else
                wtof("UFSD131I SUPERBLOCK WRITTEN FOR DSN=%s", d->dsn);
        }
        /* Save ddname before close_disk frees the struct */
        memcpy(ddname, d->ddname, 9);
        close_disk(d);
        /* Free DYNALLOC'd DDs (generated names start with "UFD") */
        if (memcmp(ddname, "UFD", 3) == 0)
            __dsfree(ddname);
        stc->disks[i] = NULL;
    }
    stc->ndisks = 0;
}

/* ============================================================
** ufsd_disk_umount
**
** AP-1f: Dynamically unmount a filesystem by mount path.
** Root filesystem ("/") cannot be unmounted.
** ============================================================ */
int
ufsd_disk_umount(UFSD_STC *stc, const char *mountpath)
{
    UFSD_DISK *disk;
    char       ddname[9];
    unsigned   i;
    int        found;

    if (!stc || !mountpath) return 8;

    if (mountpath[0] == '/' && mountpath[1] == '\0') {
        wtof("UFSD132E UNMOUNT: CANNOT UNMOUNT ROOT FILESYSTEM");
        return 8;
    }

    found = -1;
    for (i = 0; i < stc->ndisks; i++) {
        if (stc->disks[i] &&
            strcmp(stc->disks[i]->mountpath, mountpath) == 0) {
            found = (int)i;
            break;
        }
    }

    if (found < 0) {
        wtof("UFSD133E UNMOUNT: NO FILESYSTEM MOUNTED ON %s", mountpath);
        return 8;
    }

    disk = stc->disks[found];
    wtof("UFSD064I UNMOUNTING %s FROM %s", disk->dsn, disk->mountpath);

    /* Write superblock back to disk for RW filesystems */
    if ((disk->flags & UFSD_DISK_OPEN) &&
        !(disk->flags & UFSD_DISK_RDONLY)) {
        if (ufsd_sb_write(disk))
            wtof("UFSD130W SUPERBLOCK WRITEBACK FAILED FOR DSN=%s",
                 disk->dsn);
        else
            wtof("UFSD131I SUPERBLOCK WRITTEN FOR DSN=%s", disk->dsn);
    }

    /* Save ddname, close, DYNFREE if applicable */
    memcpy(ddname, disk->ddname, 9);
    close_disk(disk);
    if (memcmp(ddname, "UFD", 3) == 0)
        __dsfree(ddname);

    /* Compact the array.  The mount point table is indexed the same
    ** way, so it has to move with it (issue #52) -- and before
    ** ndisks drops, which is the table's own bound. */
    ufsd_mount_remove(stc->mountpt, stc->ndisks, (unsigned)found);

    for (i = (unsigned)found; i < stc->ndisks - 1U; i++)
        stc->disks[i] = stc->disks[i + 1U];
    stc->disks[--stc->ndisks] = NULL;

    return 0;
}
