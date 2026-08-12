/* UFSFMTG.C - UFS370 format geometry
**
** The arithmetic half of UFSFMT (see src/ufsfmt.c), plus the two
** report helpers that have to decide what an absent owner looks like
** (#62).  Portable C: no MVS headers, no I/O, no 64-bit time -- so
** `make test-host` can run it natively (test/mvs/tstufsg.c).  The
** string work here never reaches a disk; every byte that does is
** written by src/ufsfmt.c.
**
** The layout produced here is byte-compatible with ufsd-utils
** (pkg/ufs/image.go, func Create), which is the reference
** implementation.  Where the two could diverge, this file follows Go.
*/

#include "ufsfmt.h"
#include <string.h>

/* ============================================================
** ufsfmt_geometry
**
** Disk layout:
**
**   sector 0                    boot block + boot extension
**   sector 1                    superblock
**   sector 2 .. datablock_start-1   ilist (inode blocks)
**   datablock_start .. end      data blocks
**
** Three counters in this function are the ones #50 exists to get
** right; each is computed from the layout rather than accumulated
** while writing, so a loop that ends early can no longer corrupt
** them:
**
**   total_freeinode  inode_slots - 2.  Slots 0 and 1 of the first
**                    inode block are physically occupied (inode 1 =
**                    BALBLK monument, inode 2 = root), so exactly two
**                    are unavailable -- not three, which is what the
**                    original produced by counting slots and then
**                    letting the allocator decrement once more.
**
**   total_freeblock  total_blocks - datablock_start - 1 (the -1 is the
**                    root directory block).  The original accumulated
**                    this inside the free-chain loop and wrote 0 for
**                    any disk with 51 or fewer data blocks, because
**                    the accumulator was only touched on the branch
**                    that fills a whole chain block.
**
**   nfreeblock       the superblock cache holds the first up-to-51
**                    data blocks ascending; the root directory block
**                    is then popped LIFO from the top.
** ============================================================ */
int
ufsfmt_geometry(UFSFMT_GEOM *g, unsigned total_blocks,
                unsigned blksize, double inode_pct)
{
    double   ib;
    unsigned n;
    unsigned i;

    if (!g) return UFSFMT_GEOM_ARG;
    memset(g, 0, sizeof(*g));

    if (blksize < UFSFMT_MIN_BLKSIZE || blksize > UFSFMT_MAX_BLKSIZE
        || (blksize % UFSFMT_MIN_BLKSIZE) != 0U)
        return UFSFMT_GEOM_BLKSIZE;

    if (inode_pct < UFSFMT_MIN_INODEPCT || inode_pct > UFSFMT_MAX_INODEPCT)
        return UFSFMT_GEOM_PCT;

    if (total_blocks < UFSFMT_MIN_BLOCKS)
        return UFSFMT_GEOM_TOOSMALL;

    g->blksize          = blksize;
    g->total_blocks     = total_blocks;
    g->inodes_per_block = blksize / UFSFMT_INODE_SIZE;

    /* Inode blocks.  INODES is not a percentage of the volume: it is a
    ** percentage of the blocks that would be needed to give every block
    ** its own inode.  Operation order and width follow image.go
    ** (math.Round(float64(total)/float64(ipb) * pct / 100.0)) so the
    ** two implementations round identically.  Both operands of the
    ** first division are exact in binary and in hex floating point;
    ** only an input landing exactly on .5 could round differently. */
    ib = (double)total_blocks / (double)g->inodes_per_block
         * inode_pct / 100.0;
    g->inode_blocks = (unsigned)(ib + 0.5);
    if (g->inode_blocks < 2U)
        g->inode_blocks = 2U;                 /* ufs370 minimum ilist */

    g->inode_slots     = g->inode_blocks * g->inodes_per_block;
    g->datablock_start = UFSFMT_ILIST_SECTOR + g->inode_blocks;

    /* One data block for the root directory is the bare minimum. */
    if (g->datablock_start >= total_blocks) {
        memset(g, 0, sizeof(*g));
        return UFSFMT_GEOM_TOOSMALL;
    }

    for (n = blksize; n > 1U; n >>= 1)
        g->blksize_shift++;                   /* 4096 -> 12 */

    /* Superblock free block cache: the first up-to-51 data blocks,
    ** ascending.  Anything beyond that lives in the V7 chain blocks
    ** the writer lays down starting at datablock_start. */
    g->sb_cache_blocks = total_blocks - g->datablock_start;
    if (g->sb_cache_blocks > UFSFMT_MAX_FREEBLOCK)
        g->sb_cache_blocks = UFSFMT_MAX_FREEBLOCK;

    for (i = 0; i < g->sb_cache_blocks; i++)
        g->freeblock[i] = g->datablock_start + i;
    g->nfreeblock = g->sb_cache_blocks;

    /* Root directory data block: popped LIFO off the top of the cache,
    ** so on any disk with 51+ data blocks it is datablock_start + 50.
    **
    ** freeblock[nfreeblock] deliberately keeps the popped value.  UFSD
    ** only ever reads entries below nfreeblock, and ufsd-utils leaves
    ** the stale entry in place too -- zeroing it here would make the
    ** superblock differ from the reference image. */
    g->nfreeblock--;
    g->root_block = g->freeblock[g->nfreeblock];

    g->total_freeblock = total_blocks - g->datablock_start - 1U;
    g->total_freeinode = g->inode_slots - 2U;

    /* First block that a chain block has to account for.  Equal to
    ** total_blocks when the whole free list fits in the superblock. */
    g->chain_start = g->datablock_start + g->sb_cache_blocks;

    /* Superblock free inode cache.  Every slot except 0 and 1 of the
    ** first inode block is free, and inode numbers are 1-based, so the
    ** cache is simply 3, 4, 5, ... -- which is what ufsd-utils'
    ** seedFreeInodeCache arrives at by scanning the ilist for mode==0.
    **
    ** Seeding is not required for correctness: ufsd_sb_alloc_inode()
    ** refills from an ilist scan when the cache is empty.  It is done
    ** because the reference image does it, and it saves the first
    ** FOPEN on a fresh disk one full scan. */
    n = g->inode_slots - 2U;
    if (n > UFSFMT_MAX_FREEINODE)
        n = UFSFMT_MAX_FREEINODE;
    for (i = 0; i < n; i++)
        g->freeinode[i] = i + UFSFMT_ROOT_INO + 1U;   /* 3, 4, 5, ... */
    g->nfreeinode = n;

    return UFSFMT_GEOM_OK;
}

/* ============================================================
** ufsfmt_owner_text
**
** See include/ufsfmt.h.  A NULL owner is treated as an absent one
** rather than refused: this is called from the report, and a report
** is not the place to fault.
** ============================================================ */
const char *
ufsfmt_owner_text(const char *owner)
{
    return (owner && owner[0]) ? owner : UFSFMT_UNOWNED;
}

/* Append what fits of s, keeping out NUL-terminated within outsz.
** Built by hand rather than with snprintf: the statement is assembled
** from a handful of literals, and this stays within C89 stdio, which
** is what cc370 provides. */
static void
app(char *out, unsigned outsz, unsigned *len, const char *s)
{
    while (*s && *len + 1U < outsz)
        out[(*len)++] = *s++;
    out[*len] = '\0';
}

/* ============================================================
** ufsfmt_mount_stmt
**
** See include/ufsfmt.h.  Truncation is silent by design: the caller
** is a report line, and a statement cut short is visibly incomplete
** to the operator reading it, while a buffer run past its end is not
** visible at all.
** ============================================================ */
void
ufsfmt_mount_stmt(char *out, unsigned outsz,
                  const char *dsn, const char *owner)
{
    unsigned len = 0U;

    if (!out || outsz == 0U) return;
    out[0] = '\0';

    app(out, outsz, &len, "MOUNT    DSN(");
    app(out, outsz, &len, (dsn && dsn[0]) ? dsn : "your.dataset.name");
    app(out, outsz, &len, ") PATH(/your/mount/point) MODE(RW)");

    if (owner && owner[0]) {
        app(out, outsz, &len, " OWNER(");
        app(out, outsz, &len, owner);
        app(out, outsz, &len, ")");
    }
}
