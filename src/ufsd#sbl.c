/* UFSD#SBL.C - UFS Superblock Management
**
** AP-1e: Read, write, and manage the UFS superblock.
** The superblock is cached in disk->sb (read at mount time).
** Alloc/free functions modify disk->sb in memory only;
** caller must call ufsd_sb_write() to persist changes to disk.
**
** ufsd_sb_read       read sector 1 into disk->sb
** ufsd_sb_write      write disk->sb back to sector 1
** ufsd_sb_alloc_block pop one block from the free block cache
** ufsd_sb_free_block  push one block onto the free block cache
** ufsd_sb_alloc_inode pop one inode number from the free inode cache
** ufsd_sb_free_inode  push one inode number onto the free inode cache
**
** AP-1e Simplification:
**   If nfreeblock == 0, alloc_block returns UFSD_RC_NOSPACE.
**   Cache refill from the disk is deferred (not implemented).
**   On a freshly formatted disk, mkufs pre-populates the cache.
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================
** ufsd_sb_read
**
** Read disk sector 1 into a temporary buffer and copy the
** first sizeof(UFSD_SB) bytes into disk->sb.
** Returns UFSD_RC_OK or UFSD_RC_IO.
** ============================================================ */
int
ufsd_sb_read(UFSD_DISK *disk)
{
    char *buf;
    int   rc;

    if (!disk) return UFSD_RC_IO;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return UFSD_RC_IO;

    rc = ufsd_blk_read(disk, 1U, buf);
    if (rc == UFSD_RC_OK)
        memcpy(&disk->sb, buf, sizeof(UFSD_SB));

    free(buf);
    return rc;
}

/* ============================================================
** ufsd_sb_write
**
** Read sector 1, overlay the first sizeof(UFSD_SB) bytes with
** disk->sb, and write the full block back.  The read-modify-
** write preserves any data beyond the 512-byte superblock area.
** Returns UFSD_RC_OK or UFSD_RC_IO.
** ============================================================ */
int
ufsd_sb_write(UFSD_DISK *disk)
{
    char *buf;
    int   rc;

    if (!disk) return UFSD_RC_IO;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return UFSD_RC_IO;

    rc = ufsd_blk_read(disk, 1U, buf);
    if (rc == UFSD_RC_OK) {
        memcpy(buf, &disk->sb, sizeof(UFSD_SB));
        rc = ufsd_blk_write(disk, 1U, buf);
    }

    free(buf);
    return rc;
}

/* ============================================================
** ufsd_sb_alloc_block
**
** Pop the last entry from the free block cache (disk->sb.freeblock[]).
** Decrements nfreeblock and total_freeblock in the in-memory sb.
** The caller must call ufsd_sb_write() to persist.
**
** Returns UFSD_RC_OK and stores the sector number in *out_sector,
** or UFSD_RC_NOSPACE if the cache is empty.
** ============================================================ */
int
ufsd_sb_alloc_block(UFSD_DISK *disk, unsigned *out_sector)
{
    if (!disk || !out_sector) return UFSD_RC_IO;
    if (disk->sb.nfreeblock == 0) return UFSD_RC_NOSPACE;

    disk->sb.nfreeblock--;
    *out_sector = disk->sb.freeblock[disk->sb.nfreeblock];
    disk->sb.freeblock[disk->sb.nfreeblock] = 0;
    if (disk->sb.total_freeblock > 0)
        disk->sb.total_freeblock--;

    return UFSD_RC_OK;
}

/* ============================================================
** ufsd_sb_free_block
**
** Push a block sector number back into the free block cache.
** If the cache is full, the block is effectively leaked until
** the next remount (acceptable for Phase 1 PoC).
** The caller must call ufsd_sb_write() to persist.
** ============================================================ */
void
ufsd_sb_free_block(UFSD_DISK *disk, unsigned sector)
{
    if (!disk || sector == 0) return;
    if (disk->sb.nfreeblock < UFSD_SB_MAX_FREEBLOCK) {
        disk->sb.freeblock[disk->sb.nfreeblock++] = sector;
        disk->sb.total_freeblock++;
    }
    /* Cache overflow: sector leaked for this session. */
}

/* ============================================================
** ufsd_sb_alloc_inode
**
** Pop the last entry from the free inode cache.
** Returns UFSD_RC_OK and stores the inode number in *out_ino,
** or UFSD_RC_NOINODES if the cache is empty.
** The caller must call ufsd_sb_write() to persist.
** ============================================================ */
int
ufsd_sb_alloc_inode(UFSD_DISK *disk, unsigned *out_ino)
{
    if (!disk || !out_ino) return UFSD_RC_IO;
    if (disk->sb.nfreeinode == 0) return UFSD_RC_NOINODES;

    disk->sb.nfreeinode--;
    *out_ino = disk->sb.freeinode[disk->sb.nfreeinode];
    disk->sb.freeinode[disk->sb.nfreeinode] = 0;
    if (disk->sb.total_freeinode > 0)
        disk->sb.total_freeinode--;

    return UFSD_RC_OK;
}

/* ============================================================
** ufsd_sb_free_inode
**
** Push an inode number back into the free inode cache.
** If the cache is full, the inode is effectively leaked.
** The caller must call ufsd_sb_write() to persist.
** ============================================================ */
void
ufsd_sb_free_inode(UFSD_DISK *disk, unsigned ino)
{
    if (!disk || ino == 0) return;
    if (disk->sb.nfreeinode < UFSD_SB_MAX_FREEINODE) {
        disk->sb.freeinode[disk->sb.nfreeinode++] = ino;
        disk->sb.total_freeinode++;
    }
    /* Cache overflow: inode leaked for this session. */
}
