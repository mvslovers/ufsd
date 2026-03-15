/* UFSD#SBL.C - UFS Superblock Management
**
** AP-1e: Read, write, and manage the UFS superblock.
** The superblock is cached in disk->sb (read at mount time).
** Alloc/free functions modify disk->sb in memory only;
** caller must call ufsd_sb_write() to persist changes to disk.
**
** ufsd_sb_read         read sector 1 into disk->sb
** ufsd_sb_write        write disk->sb back to sector 1
** ufsd_sb_alloc_block  pop one block from the free block cache
** ufsd_sb_free_block   push one block onto the free block cache
** ufsd_sb_alloc_inode  pop one inode number from the free inode cache
** ufsd_sb_free_inode   push one inode number onto the free inode cache
** ufsd_sb_rebuild_free scan disk and rebuild free block cache
**
** AP-2a: Free block cache refill via V7 chain blocks.
**   When nfreeblock drops to 0, the last popped block is a chain
**   block whose contents are: [0]=count, [1..count]=block numbers.
**   alloc_block reads this chain to refill the superblock cache.
**   mkufs (ufs370) creates these chain blocks at format time.
**
**   If the V7 chain is broken (e.g. from a previous UFSD version
**   that did not implement chain refill), alloc_block falls back
**   to ufsd_sb_rebuild_free which scans the inode table to find
**   unreferenced data blocks.
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>
#include <clibwto.h>

/* Forward declaration */
static int sb_scan_refill(UFSD_DISK *disk);

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
** chain_refill
**
** Attempt V7 chain block refill: read block 'chain_blk' and
** interpret it as a free list chain entry.
** Returns 1 if refill succeeded, 0 if the chain is broken.
** ============================================================ */
static int
chain_refill(UFSD_DISK *disk, unsigned chain_blk)
{
    char     *buf;
    unsigned *chain;
    unsigned  new_count;
    unsigned  i;
    unsigned  vol;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return 0;

    if (ufsd_blk_read(disk, chain_blk, buf) != UFSD_RC_OK) {
        free(buf);
        return 0;
    }

    chain     = (unsigned *)buf;
    new_count = chain[0];
    vol       = disk->sb.volume_size;

    /* Validate: count must be in range, and all block numbers
    ** must be within the volume.  If any entry is out of range,
    ** the chain is broken (block was overwritten with file data). */
    if (new_count == 0 || new_count > UFSD_SB_MAX_FREEBLOCK) {
        free(buf);
        return 0;
    }
    for (i = 0; i < new_count; i++) {
        if (chain[i + 1] >= vol) {
            free(buf);
            return 0;
        }
    }

    /* Chain looks valid: refill the cache */
    for (i = 0; i < new_count; i++)
        disk->sb.freeblock[i] = chain[i + 1];
    disk->sb.nfreeblock = new_count;

    free(buf);
    return 1;
}

/* ============================================================
** ufsd_sb_alloc_block
**
** Pop the last entry from the free block cache (disk->sb.freeblock[]).
** Decrements nfreeblock and total_freeblock in the in-memory sb.
** The caller must call ufsd_sb_write() to persist.
**
** When the cache is empty, attempts refill in two ways:
**   1. V7 chain block (from the just-popped block)
**   2. Inode-table scan (sb_scan_refill) as fallback
**
** Returns UFSD_RC_OK and stores the sector number in *out_sector,
** or UFSD_RC_NOSPACE if the cache is empty and cannot be refilled.
** ============================================================ */
int
ufsd_sb_alloc_block(UFSD_DISK *disk, unsigned *out_sector)
{
    if (!disk || !out_sector) return UFSD_RC_IO;

    /* Cache empty on entry: try inode-table scan to recover */
    if (disk->sb.nfreeblock == 0) {
        if (sb_scan_refill(disk) != UFSD_RC_OK)
            return UFSD_RC_NOSPACE;
    }

    disk->sb.nfreeblock--;
    *out_sector = disk->sb.freeblock[disk->sb.nfreeblock];
    disk->sb.freeblock[disk->sb.nfreeblock] = 0;
    if (disk->sb.total_freeblock > 0)
        disk->sb.total_freeblock--;

    /* V7 chain refill: when the cache empties after a pop, the
    ** block we just popped may be a chain block.  Try chain first,
    ** fall back to scan if the chain is broken. */
    if (disk->sb.nfreeblock == 0 && *out_sector != 0) {
        if (!chain_refill(disk, *out_sector)) {
            sb_scan_refill(disk);  /* best effort */
        }
    }

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
** inode_scan_refill  (static)
**
** Refill the free inode cache by scanning the inode list on disk.
** Reads each inode block and checks the mode field: mode == 0
** means the inode is free.  Fills the cache with up to
** UFSD_SB_MAX_FREEINODE entries.
**
** Analogous to ufs_freeinode_update() in ufs370.
** Returns UFSD_RC_OK if at least one free inode was found.
** ============================================================ */
static int
inode_scan_refill(UFSD_DISK *disk)
{
    char        *buf;
    UFSD_DINODE *dip;
    unsigned     ipb;
    unsigned     sector;
    unsigned     i;
    unsigned     ino;
    unsigned     found;

    ipb = disk->sb.inodes_per_block;
    if (ipb == 0) ipb = (unsigned)disk->blksize / UFSD_INODE_SIZE;

    buf = (char *)malloc(disk->blksize);
    if (!buf) return UFSD_RC_IO;

    found = 0;

    for (sector = disk->sb.ilist_sector;
         sector < disk->sb.datablock_start
         && disk->sb.nfreeinode < UFSD_SB_MAX_FREEINODE;
         sector++) {

        if (ufsd_blk_read(disk, sector, buf) != UFSD_RC_OK)
            continue;

        for (i = 0;
             i < ipb && disk->sb.nfreeinode < UFSD_SB_MAX_FREEINODE;
             i++) {
            ino = (sector - disk->sb.ilist_sector) * ipb + i + 1U;
            if (ino <= 1U) continue;  /* skip reserved inode 1 */

            dip = (UFSD_DINODE *)(buf + i * UFSD_INODE_SIZE);
            if (dip->mode == 0) {
                disk->sb.freeinode[disk->sb.nfreeinode++] = ino;
                found++;
            }
        }
    }

    free(buf);

    if (found > 0) {
        wtof("UFSD076I Free inode scan: found %u inodes "
             "(cache refilled to %u)", found, disk->sb.nfreeinode);
        return UFSD_RC_OK;
    }

    return UFSD_RC_NOINODES;
}

/* ============================================================
** ufsd_sb_alloc_inode
**
** Pop the last entry from the free inode cache.
** When the cache is empty, scans the inode list on disk to
** find free inodes (mode == 0) and refills the cache.
** Returns UFSD_RC_OK and stores the inode number in *out_ino,
** or UFSD_RC_NOINODES if no free inodes exist.
** The caller must call ufsd_sb_write() to persist.
** ============================================================ */
int
ufsd_sb_alloc_inode(UFSD_DISK *disk, unsigned *out_ino)
{
    if (!disk || !out_ino) return UFSD_RC_IO;

    if (disk->sb.nfreeinode == 0) {
        if (inode_scan_refill(disk) != UFSD_RC_OK)
            return UFSD_RC_NOINODES;
    }

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

/* ============================================================
** sb_scan_refill  (static)
**
** Emergency free block cache refill by scanning the inode table.
** Builds a bitmap of all blocks referenced by allocated inodes,
** then fills the superblock cache with unreferenced data blocks.
**
** This is the fallback when the V7 chain is broken (e.g. after
** running a UFSD version that did not implement chain refill).
**
** The scan covers all inodes (ilist_sector to datablock_start)
** and marks direct + single-indirect blocks as used.
**
** Returns UFSD_RC_OK if at least one free block was found,
** UFSD_RC_NOSPACE if the disk is genuinely full.
** ============================================================ */
static int
sb_scan_refill(UFSD_DISK *disk)
{
    unsigned char *used;
    unsigned       bitmap_bytes;
    unsigned       vol;
    unsigned       ipb;
    unsigned       ilist_blocks;
    unsigned       max_ino;
    unsigned       ino;
    unsigned       blk;
    unsigned       i;
    unsigned       found;
    UFSD_DINODE    dino;

    vol = disk->sb.volume_size;
    if (vol == 0) return UFSD_RC_NOSPACE;

    bitmap_bytes = (vol + 7U) / 8U;
    used = (unsigned char *)calloc(1U, bitmap_bytes);
    if (!used) return UFSD_RC_IO;

    /* Mark metadata blocks as used */
    /* Block 0 (boot) and block 1 (superblock) */
    used[0] |= 0x03U;

    /* Ilist blocks: from ilist_sector to datablock_start - 1 */
    ipb = disk->sb.inodes_per_block;
    if (ipb == 0) ipb = (unsigned)disk->blksize / UFSD_INODE_SIZE;
    ilist_blocks = disk->sb.datablock_start - disk->sb.ilist_sector;
    max_ino      = ilist_blocks * ipb;

    for (i = 0; i < ilist_blocks; i++) {
        blk = disk->sb.ilist_sector + i;
        if (blk < vol)
            used[blk / 8U] |= (unsigned char)(1U << (blk % 8U));
    }

    /* Scan all inodes: mark their data blocks as used */
    for (ino = 1; ino <= max_ino; ino++) {
        if (ufsd_ino_read(disk, ino, &dino) != UFSD_RC_OK)
            continue;
        if (dino.nlink == 0 && dino.mode == 0)
            continue;  /* free inode */

        /* Direct blocks */
        for (i = 0; i < UFSD_NADDR_DIRECT; i++) {
            blk = dino.addr[i];
            if (blk != 0 && blk < vol)
                used[blk / 8U] |= (unsigned char)(1U << (blk % 8U));
        }

        /* Single indirect block + its entries */
        if (dino.addr[UFSD_NADDR_DIRECT] != 0
            && dino.addr[UFSD_NADDR_DIRECT] < vol) {
            char     *ibuf;
            unsigned *ind;

            blk = dino.addr[UFSD_NADDR_DIRECT];
            used[blk / 8U] |= (unsigned char)(1U << (blk % 8U));

            ibuf = (char *)malloc(disk->blksize);
            if (ibuf) {
                if (ufsd_blk_read(disk, blk, ibuf) == UFSD_RC_OK) {
                    ind = (unsigned *)ibuf;
                    for (i = 0; i < (unsigned)disk->blksize / 4U; i++) {
                        if (ind[i] != 0 && ind[i] < vol)
                            used[ind[i] / 8U] |=
                                (unsigned char)(1U << (ind[i] % 8U));
                    }
                }
                free(ibuf);
            }
        }
    }

    /* Collect free blocks into the cache */
    disk->sb.nfreeblock = 0;
    found = 0;
    for (i = disk->sb.datablock_start;
         i < vol && disk->sb.nfreeblock < UFSD_SB_MAX_FREEBLOCK;
         i++) {
        if (!(used[i / 8U] & (1U << (i % 8U)))) {
            disk->sb.freeblock[disk->sb.nfreeblock++] = i;
            found++;
        }
    }

    /* Remember where we stopped scanning, so subsequent refills
    ** continue from there instead of rescanning the same range.
    ** We reuse the unused3 field in the superblock for this.
    ** (Not persisted to disk — only valid for this session.) */
    disk->sb.unused3 = i;

    free(used);

    if (found > 0) {
        wtof("UFSD070I Free block scan: found %u blocks "
             "(cache refilled to %u)", found, disk->sb.nfreeblock);
        return UFSD_RC_OK;
    }

    return UFSD_RC_NOSPACE;
}

/* ============================================================
** ufsd_sb_rebuild_free
**
** Public entry point for the free block + inode cache rebuild.
** Called from MODIFY REBUILD command (ufsd#cmd.c).
** Forces a full disk scan regardless of current cache state.
** Persists the result via ufsd_sb_write.
**
** Returns UFSD_RC_OK or UFSD_RC_NOSPACE.
** ============================================================ */
int
ufsd_sb_rebuild_free(UFSD_DISK *disk)
{
    int rc;

    if (!disk) return UFSD_RC_IO;

    /* Rebuild free block cache */
    disk->sb.nfreeblock = 0;
    disk->sb.unused3    = 0;  /* reset scan position */
    rc = sb_scan_refill(disk);

    /* Rebuild free inode cache */
    disk->sb.nfreeinode = 0;
    inode_scan_refill(disk);

    if (rc == UFSD_RC_OK)
        rc = ufsd_sb_write(disk);
    return rc;
}
