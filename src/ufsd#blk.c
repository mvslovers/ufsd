/* UFSD#BLK.C - BDAM Block I/O
**
** AP-1e: Low-level block read/write for UFS disk datasets.
**
** Each operation:
**   1. Fills a DECB and issues osdread/osdwrite (async, returns immediately)
**   2. Calls oscheck to wait for completion and check for errors
**
** ufsd_blk_read (UFSD@BRD)  read one block by relative sector number
** ufsd_blk_write(UFSD@BWR)  write one block by relative sector number
**
** The caller must provide a buffer of disk->blksize bytes.
** Both functions return UFSD_RC_OK on success, UFSD_RC_IO on error.
*/

#include "ufsd.h"
#include <string.h>
#include <osio.h>
#include <osdcb.h>

/* ============================================================
** ufsd_blk_read
**
** Read one physical block from disk at the given sector number
** into buf (must be at least disk->blksize bytes).
** Returns UFSD_RC_OK or UFSD_RC_IO.
** ============================================================ */
int
ufsd_blk_read(UFSD_DISK *disk, unsigned sector, void *buf)
{
    DECB decb;

    if (!disk || !disk->dcb || !buf) return UFSD_RC_IO;

    memset(&decb, 0, sizeof(decb));
    osdread(&decb, (DCB *)disk->dcb, buf, (int)disk->blksize, sector);
    return oscheck(&decb) ? UFSD_RC_IO : UFSD_RC_OK;
}

/* ============================================================
** ufsd_blk_write
**
** Write one physical block to disk at the given sector number
** from buf (must be disk->blksize bytes).
** Refuses writes to read-only disks (UFSD_DISK_RDONLY).
** Returns UFSD_RC_OK or UFSD_RC_IO.
** ============================================================ */
int
ufsd_blk_write(UFSD_DISK *disk, unsigned sector, void *buf)
{
    DECB decb;

    if (!disk || !disk->dcb || !buf) return UFSD_RC_IO;
    if (disk->flags & UFSD_DISK_RDONLY) return UFSD_RC_IO;

    memset(&decb, 0, sizeof(decb));
    osdwrite(&decb, (DCB *)disk->dcb, buf, (int)disk->blksize, sector);
    return oscheck(&decb) ? UFSD_RC_IO : UFSD_RC_OK;
}
