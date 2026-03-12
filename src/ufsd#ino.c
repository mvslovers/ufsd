/* UFSD#INO.C - UFS Inode I/O
**
** AP-1e: Read and write on-disk inodes directly via BDAM.
** No inode cache (intentional AP-1e simplification).
** Every inode access reads/writes the physical block.
**
** Inode location on disk:
**   block  = sb.ilist_sector + (ino - 1) / (blksize / UFSD_INODE_SIZE)
**   offset = ((ino - 1) % (blksize / UFSD_INODE_SIZE)) * UFSD_INODE_SIZE
**
** ufsd_ino_read (UFSD@INR)  read one inode from disk
** ufsd_ino_write(UFSD@INW)  write one inode back to disk (read-modify-write)
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================
** ufsd_ino_read
**
** Read the on-disk inode for inode number 'ino' into *out.
** Returns UFSD_RC_OK or UFSD_RC_IO.
** ============================================================ */
int
ufsd_ino_read(UFSD_DISK *disk, unsigned ino, UFSD_DINODE *out)
{
    unsigned  ipb;     /* inodes per block */
    unsigned  blk;     /* ilist block number */
    unsigned  offset;  /* byte offset within block */
    char     *buf;
    int       rc;

    if (!disk || !out || ino == 0) return UFSD_RC_IO;

    ipb    = (unsigned)disk->blksize / UFSD_INODE_SIZE;
    blk    = disk->sb.ilist_sector + (ino - 1U) / ipb;
    offset = ((ino - 1U) % ipb) * UFSD_INODE_SIZE;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return UFSD_RC_IO;

    rc = ufsd_blk_read(disk, blk, buf);
    if (rc == UFSD_RC_OK)
        memcpy(out, buf + offset, UFSD_INODE_SIZE);

    free(buf);
    return rc;
}

/* ============================================================
** ufsd_ino_write
**
** Write the on-disk inode 'ino' from *in.
** Reads the full ilist block first, updates the inode within
** it, then writes the block back (read-modify-write).
** Returns UFSD_RC_OK or UFSD_RC_IO.
** ============================================================ */
int
ufsd_ino_write(UFSD_DISK *disk, unsigned ino, const UFSD_DINODE *in)
{
    unsigned  ipb;
    unsigned  blk;
    unsigned  offset;
    char     *buf;
    int       rc;

    if (!disk || !in || ino == 0) return UFSD_RC_IO;

    ipb    = (unsigned)disk->blksize / UFSD_INODE_SIZE;
    blk    = disk->sb.ilist_sector + (ino - 1U) / ipb;
    offset = ((ino - 1U) % ipb) * UFSD_INODE_SIZE;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return UFSD_RC_IO;

    rc = ufsd_blk_read(disk, blk, buf);
    if (rc == UFSD_RC_OK) {
        memcpy(buf + offset, in, UFSD_INODE_SIZE);
        rc = ufsd_blk_write(disk, blk, buf);
    }

    free(buf);
    return rc;
}
