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

/* DD name sequence counter for DYNALLOC */
static unsigned s_ddn_seq = 0;

/* Forward declarations */
static UFSD_DISK *open_disk(const char *ddname);
static void       close_disk(UFSD_DISK *disk);
static int        mkdir_p(UFSD_DISK *disk, const char *path);

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
            wtof("UFSD120E DYNALLOC failed for DSN=%s "
                 "(S99ERR=%04X: %s)", dsname,
                 (unsigned)rb.error, msg);
        else
            wtof("UFSD120E DYNALLOC failed for DSN=%s "
                 "(S99ERR=%04X)", dsname,
                 (unsigned)rb.error);

        FreeTXT99Array(&txt);
        return 8;
    }

    FreeTXT99Array(&txt);
    return 0;

bad_setup:
    wtof("UFSD120E DYNALLOC failed for DSN=%s "
         "(text unit build error)", dsname);
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
        wtof("UFSD042E Cannot allocate disk handle for %s", ddname);
        return NULL;
    }

    /* Store DD name (8 chars + NUL) */
    memcpy(disk->ddname, ddname, 8);
    disk->ddname[8] = '\0';

    /* Allocate BDAM DCB */
    dcb = osddcb(disk->ddname);
    if (!dcb) {
        wtof("UFSD042E Cannot allocate DCB for %s", disk->ddname);
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
        wtof("UFSD043E Cannot open %s", disk->ddname);
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

    /* Read and validate boot block (sector 0) */
    buf = (char *)calloc(1, (unsigned)disk->blksize);
    if (buf) {
        memset(&decb, 0, sizeof(decb));
        osdread(&decb, dcb, buf, (int)disk->blksize, 0);
        oscheck(&decb);

        boot = (UFSBOOT_HDR *)buf;
        if (boot->type != (unsigned short)UFSD_DISK_TYPE_UFS ||
            (unsigned)(boot->type + boot->check) != 0xFFFFU) {
            wtof("UFSD044E %s: not a valid UFS disk (type=%04X)",
                 disk->ddname, (unsigned)boot->type);
            free(buf);
            close_disk(disk);
            return NULL;
        }
        free(buf);
    }

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
            /* Fix up timestamps/owner if missing (legacy dirs) */
            if (ufsd_ino_read(disk, found, &dino) == UFSD_RC_OK
                && dino.owner[0] == '\0') {
                mtime64_t now;
                mtime64(&now);
                dino.ctime.v2 = now;
                dino.mtime.v2 = now;
                dino.atime.v2 = now;
                strcpy(dino.owner, "UFSD");
                strcpy(dino.group, "SYS1");
                ufsd_ino_write(disk, found, &dino);
            }
            cur_ino = found;
            continue;
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
        strcpy(dino.owner, "UFSD");
        strcpy(dino.group, "SYS1");

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
        wtof("UFSD065E MOUNT: path must be absolute");
        return 8;
    }
    if (stc->ndisks >= (unsigned)UFSD_MAX_DISKS) {
        wtof("UFSD061E MOUNT: disk table full (%u slots)",
             (unsigned)UFSD_MAX_DISKS);
        return 8;
    }

    /* Check for duplicate mount path */
    for (i = 0; i < stc->ndisks; i++) {
        if (stc->disks[i] &&
            strcmp(stc->disks[i]->mountpath, mountpath) == 0) {
            wtof("UFSD067E MOUNT: %s already mounted", mountpath);
            return 8;
        }
    }

    /* Generate DD name: UFD00001, UFD00002, ... */
    sprintf(ddname, "UFD%05u", ++s_ddn_seq);

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
        wtof("UFSD124E Superblock read/validation failed for DSN=%s",
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

    stc->disks[stc->ndisks++] = disk;

    if (disk->mount_owner[0])
        wtof("UFSD060I Mounted %s on %s (%s, OWNER=%s)",
             disk->dsn, disk->mountpath,
             mode == UFSD_MOUNT_RW ? "RW" : "RO",
             disk->mount_owner);
    else
        wtof("UFSD060I Mounted %s on %s (%s)",
             disk->dsn, disk->mountpath,
             mode == UFSD_MOUNT_RW ? "RW" : "RO");

    return 0;
}

/* ============================================================
** ufsd_ufs_init
**
** AP-3a: Read parmlib configuration, mount root filesystem
** (auto-create if missing), create mount-point directories,
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
        wtof("UFSD061E Parmlib (DD:UFSDPRM) not found -- shutting down");
        return 8;
    }

    ufsd_cfg_dump(&cfg);

    /* Mount root filesystem via DYNALLOC */
    rc = ufsd_disk_mount_dyn(stc, cfg.root_dsname, "/",
                             UFSD_MOUNT_RW, "");
    if (rc != 0) {
        wtof("UFSD061E Cannot mount root filesystem -- shutting down");
        return 8;
    }

    /* Mark as root */
    root = stc->disks[0];
    root->flags |= UFSD_DISK_ROOT;

    /* Create mount-point directories on root for each MOUNT */
    for (i = 0; i < cfg.nmounts; i++) {
        const char *mpath = cfg.mounts[i].path;
        rc = mkdir_p(root, mpath);
        if (rc != UFSD_RC_OK && rc != UFSD_RC_EXIST)
            wtof("UFSD122W Cannot create mount point %s (rc=%d)",
                 mpath, rc);
    }

    /* Root is now RO for clients — mount-point dirs are created above */
    root->mount_mode = UFSD_MOUNT_RO;

    /* Mount each configured filesystem */
    for (i = 0; i < cfg.nmounts; i++) {
        UFSD_MOUNT_CFG *m = &cfg.mounts[i];
        rc = ufsd_disk_mount_dyn(stc, m->dsname, m->path,
                                 m->mode, m->owner);
        if (rc != 0)
            wtof("UFSD123W Cannot mount DSN=%s on %s",
                 m->dsname, m->path);
    }

report:
    wtof("UFSD040I %u filesystem(s) mounted", stc->ndisks);
    for (i = 0; i < stc->ndisks; i++) {
        UFSD_DISK *d = stc->disks[i];
        if (!d) continue;
        if (d->flags & UFSD_DISK_ROOT)
            wtof("UFSD041I   %s DSN=%s (root, %s)",
                 d->mountpath, d->dsn,
                 d->mount_mode == UFSD_MOUNT_RW ? "RW" : "RO");
        else if (d->mount_owner[0])
            wtof("UFSD041I   %s DSN=%s (%s, OWNER=%s)",
                 d->mountpath, d->dsn,
                 d->mount_mode == UFSD_MOUNT_RW ? "RW" : "RO",
                 d->mount_owner);
        else
            wtof("UFSD041I   %s DSN=%s (%s)",
                 d->mountpath, d->dsn,
                 d->mount_mode == UFSD_MOUNT_RW ? "RW" : "RO");
    }

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
                wtof("UFSD130W Superblock writeback failed for DSN=%s",
                     d->dsn);
            else
                wtof("UFSD131I Superblock written for DSN=%s", d->dsn);
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
        wtof("UFSD132E UNMOUNT: cannot unmount root filesystem");
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
        wtof("UFSD133E UNMOUNT: no filesystem mounted on %s", mountpath);
        return 8;
    }

    disk = stc->disks[found];
    wtof("UFSD064I Unmounting %s from %s", disk->dsn, disk->mountpath);

    /* Write superblock back to disk for RW filesystems */
    if ((disk->flags & UFSD_DISK_OPEN) &&
        !(disk->flags & UFSD_DISK_RDONLY)) {
        if (ufsd_sb_write(disk))
            wtof("UFSD130W Superblock writeback failed for DSN=%s",
                 disk->dsn);
        else
            wtof("UFSD131I Superblock written for DSN=%s", disk->dsn);
    }

    /* Save ddname, close, DYNFREE if applicable */
    memcpy(ddname, disk->ddname, 9);
    close_disk(disk);
    if (memcmp(ddname, "UFD", 3) == 0)
        __dsfree(ddname);

    /* Compact the array */
    for (i = (unsigned)found; i < stc->ndisks - 1U; i++)
        stc->disks[i] = stc->disks[i + 1U];
    stc->disks[--stc->ndisks] = NULL;

    return 0;
}
