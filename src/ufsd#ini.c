/* UFSD#INI.C - UFS Disk Initialization
**
** AP-1d Step 2: Open UFSDISK0-9 BDAM datasets at STC startup.
** Close and free all disk handles at shutdown.
**
** AP-1f: Dynamic mount/unmount via ufsd_disk_mount/ufsd_disk_umount.
**
** ufsd_ufs_init(stc)           scan TIOT, open each UFSDISK[0-9] found
** ufsd_ufs_term(stc)           close and free all open disk handles
** ufsd_disk_mount(stc,dd,path) open one BDAM dataset at runtime
** ufsd_disk_umount(stc,path)   close and remove one disk at runtime
**
** WTO messages:
**   UFSD040I N disk(s) mounted at startup
**   UFSD041I   UFSDISK0 DSN=name (root)
**   UFSD041I   UFSDISK0 DSN=name (READ-ONLY)
**   UFSD042E   Cannot allocate disk handle for UFSDISK0
**   UFSD043E   Cannot open UFSDISK0
**   UFSD044W   UFSDISK0: not a valid UFS disk (type=0000)
**
** Note: only UFSDISK0 is auto-mounted at startup (as root "/").
** Additional disks (UFSDISK1-9) must be mounted explicitly via
** MODIFY MOUNT.  This avoids opening disks with no mountpath.
**   UFSD060I   Mounted DDname on /path
**   UFSD061E   DD DDname not found in TIOT
**   UFSD062E   Cannot unmount root filesystem
**   UFSD063E   No filesystem mounted on /path
**   UFSD064I   Unmounting DDname from /path
**   UFSD065E   MOUNT: path must be absolute
**   UFSD066E   MOUNT: DDname already mounted on /path
**   UFSD067E   MOUNT: /path already has a filesystem mounted
**
** Missing UFSDISK DDs are not an error; UFSD runs without any
** physical disk at AP-1d (file operations come in AP-1e).
** The first disk opened (UFSDISK0) becomes the root filesystem.
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <clibwto.h>
#include <ieftiot.h>
#include <osio.h>
#include <osdcb.h>
#include <osjfcb.h>

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

/* Forward declarations */
static int        find_dd(const char *ddname);
static UFSD_DISK *open_disk(const char *ddname);
static void       close_disk(UFSD_DISK *disk);

/* ============================================================
** find_dd
**
** Returns 1 if ddname (exactly 8 significant chars) appears
** in the TIOT for the current TCB, 0 if not found.
** ============================================================ */
static int
find_dd(const char *ddname)
{
    TIOT    *tiot = get_tiot();
    unsigned next = 0;
    TIOTDD  *dd   = (TIOTDD *)tiot->TIOTDD;

    for (; dd->TIOELNGH;
         next += (unsigned)dd->TIOELNGH,
         dd = (TIOTDD *)&tiot->TIOTDD[next]) {
        if (memcmp(ddname, dd->TIOEDDNM, 8) == 0)
            return 1;
    }
    return 0;
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
            wtof("UFSD044W %s: not a valid UFS disk (type=%04X)",
                 disk->ddname, (unsigned)boot->type);
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
** ufsd_ufs_init
**
** Scan the TIOT for UFSDISK0 through UFSDISK9.  For each DD
** found, open the BDAM dataset and store the handle in
** stc->disks[].  Missing DDs are silently skipped.
**
** The first disk opened becomes the root filesystem and is
** flagged UFSD_DISK_ROOT.
**
** Issues UFSD040I with the total count and UFSD041I per disk.
** Always returns 0 (no-disks is not a failure at AP-1d).
** ============================================================ */
int
ufsd_ufs_init(UFSD_STC *stc)
{
    char       ddname[9];  /* "UFSDISK0" + NUL */
    UFSD_DISK *disk;
    unsigned   i;

    if (!stc) return 8;

    stc->ndisks = 0;

    /* Only UFSDISK0 is auto-mounted at startup as the root filesystem.
    ** UFSDISK1-9 require an explicit /F UFSD,MOUNT command. */
    memcpy(ddname, "UFSDISK0", 9);
    if (find_dd(ddname)) {
        disk = open_disk(ddname);
        if (disk) {
            disk->flags    |= UFSD_DISK_ROOT;
            disk->mountpath[0] = '/';
            disk->mountpath[1] = '\0';
            stc->disks[stc->ndisks++] = disk;
            ufsd_sb_read(disk);
        }
    }


    wtof("UFSD040I %u disk(s) mounted", stc->ndisks);

    for (i = 0; i < stc->ndisks; i++) {
        disk = stc->disks[i];
        if (disk->flags & UFSD_DISK_ROOT)
            wtof("UFSD041I   %s DSN=%s (root)",
                 disk->ddname, disk->dsn);
        else if (disk->flags & UFSD_DISK_RDONLY)
            wtof("UFSD041I   %s DSN=%s (READ-ONLY)",
                 disk->ddname, disk->dsn);
        else
            wtof("UFSD041I   %s DSN=%s",
                 disk->ddname, disk->dsn);
    }

    return 0;
}

/* ============================================================
** ufsd_ufs_term
**
** Close all open BDAM datasets and free disk handles.
** Called at STC shutdown before the session table is freed.
** ============================================================ */
void
ufsd_ufs_term(UFSD_STC *stc)
{
    unsigned i;

    if (!stc) return;

    for (i = 0; i < stc->ndisks; i++) {
        close_disk(stc->disks[i]);
        stc->disks[i] = NULL;
    }
    stc->ndisks = 0;
}

/* ============================================================
** ufsd_disk_mount
**
** AP-1f: Dynamically mount a BDAM dataset as a filesystem.
** Scans the TIOT for ddname (blank-padded to 8 chars), opens
** the disk, reads the superblock, and appends to stc->disks[].
**
** Returns 0 on success, 8 on failure.
** ============================================================ */
int
ufsd_disk_mount(UFSD_STC *stc, const char *ddname, const char *mountpath)
{
    UFSD_DISK *disk;
    unsigned   pathlen;
    unsigned   namelen;
    unsigned   i;
    char       ddpad[9];

    if (!stc || !ddname || !mountpath) return 8;

    /* Mount path must be absolute */
    if (mountpath[0] != '/') {
        wtof("UFSD065E MOUNT: path must be absolute (must start with '/')");
        return 8;
    }

    if (stc->ndisks >= (unsigned)UFSD_MAX_DISKS) {
        wtof("UFSD061E MOUNT: disk table full (%u slots)", UFSD_MAX_DISKS);
        return 8;
    }

    /* Blank-pad DD name to 8 chars for TIOT comparison */
    memset(ddpad, ' ', 8);
    ddpad[8] = '\0';
    namelen = strlen(ddname);
    if (namelen > 8) namelen = 8;
    memcpy(ddpad, ddname, namelen);

    /* Check for duplicate DD or duplicate mount path */
    for (i = 0; i < stc->ndisks; i++) {
        if (memcmp(stc->disks[i]->ddname, ddpad, 8) == 0) {
            wtof("UFSD066E MOUNT: %.8s already mounted on %s",
                 ddpad, stc->disks[i]->mountpath);
            return 8;
        }
        if (strcmp(stc->disks[i]->mountpath, mountpath) == 0) {
            wtof("UFSD067E MOUNT: %s already has a filesystem mounted",
                 mountpath);
            return 8;
        }
    }

    if (!find_dd(ddpad)) {
        wtof("UFSD061E MOUNT: DD %.8s not found in TIOT", ddpad);
        return 8;
    }

    disk = open_disk(ddpad);
    if (!disk) return 8;

    /* Read superblock */
    ufsd_sb_read(disk);

    /* Store mount path */
    pathlen = strlen(mountpath);
    if (pathlen >= sizeof(disk->mountpath))
        pathlen = sizeof(disk->mountpath) - 1U;
    memcpy(disk->mountpath, mountpath, pathlen);
    disk->mountpath[pathlen] = '\0';

    stc->disks[stc->ndisks++] = disk;

    wtof("UFSD060I Mounted %.8s on %s (DSN=%s)",
         disk->ddname, disk->mountpath, disk->dsn);
    return 0;
}

/* ============================================================
** ufsd_disk_umount
**
** AP-1f: Dynamically unmount a filesystem by mount path.
** Finds the disk matching mountpath, closes it, and compacts
** the stc->disks[] array.
**
** The root filesystem ("/") cannot be unmounted.
** Returns 0 on success, 8 on failure.
** ============================================================ */
int
ufsd_disk_umount(UFSD_STC *stc, const char *mountpath)
{
    UFSD_DISK *disk;
    unsigned   i;
    int        found;

    if (!stc || !mountpath) return 8;

    /* Refuse to unmount root */
    if (mountpath[0] == '/' && mountpath[1] == '\0') {
        wtof("UFSD062E UNMOUNT: cannot unmount root filesystem");
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
        wtof("UFSD063E UNMOUNT: no filesystem mounted on %s", mountpath);
        return 8;
    }

    disk = stc->disks[found];
    wtof("UFSD064I Unmounting %.8s from %s", disk->ddname, disk->mountpath);

    close_disk(disk);

    /* Compact the array */
    for (i = (unsigned)found; i < stc->ndisks - 1U; i++)
        stc->disks[i] = stc->disks[i + 1U];
    stc->disks[--stc->ndisks] = NULL;

    return 0;
}
