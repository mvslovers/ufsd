/* TSTUFSG.C - UFSFMT geometry tests
**
** Covers the arithmetic UFSFMT derives a filesystem layout from
** (src/ufsfmtg.c).  Dual-target: `make test-host` runs it natively,
** `make test-mvs` runs it as a load module.
**
** The three defects issue #50 lists are all counter defects, so this
** is where they are pinned down:
**
**   1. total_freeinode was one too low.  The original counted the free
**      slots and then let the allocator decrement once more for the
**      root, arriving at slots-3 for two physically occupied slots.
**      Every case below asserts slots-2.
**
**   2. total_freeblock was written as 0 on small disks.  The original
**      only accumulated the counter on the branch that fills a whole
**      chain block, so a disk whose entire free list fits in the
**      superblock never reached it.  The "small" case is deliberately
**      below 51 data blocks and asserts a nonzero count.
**
**   3. The default group was STGADMIN, not ADMIN.  That one lives in
**      the parameter defaults (src/ufsfmt.c), not here.
**
** Expected values are cross-checked against ufsd-utils
** (pkg/ufs/image.go, func Create), the reference implementation, and
** against the SPACE planning table in samplib/ufsfmt.
**
** Timestamps are not covered: they are __64 values, and __64 is
** big-endian by construction and therefore target-verified only (see
** libc370 clib64.h).  UFSFMT keeps them out of the geometry for that
** reason, and the byte-comparison test masks them anyway.
*/

#include <mbtcheck.h>
#include <string.h>
#include "ufsfmt.h"

/* ============================================================
** check_geom
**
** Assert the whole shape of one disk in one place, so a case reads
** as a row of the test matrix rather than a wall of assertions.
** ============================================================ */
static void
check_geom(const char *name, unsigned total_blocks, unsigned blksize,
           double pct, unsigned want_inode_blocks, unsigned want_slots,
           unsigned want_dstart, unsigned want_shift,
           unsigned want_freeblock, unsigned want_freeinode,
           unsigned want_nfreeblock, unsigned want_root_block,
           unsigned want_nfreeinode)
{
    UFSFMT_GEOM g;
    char        msg[96];
    int         rc;

    rc = ufsfmt_geometry(&g, total_blocks, blksize, pct);

    sprintf(msg, "%s: geometry accepted", name);
    CHECK_EQ(rc, UFSFMT_GEOM_OK, msg);
    if (rc != UFSFMT_GEOM_OK) return;

    sprintf(msg, "%s: inodes_per_block", name);
    CHECK_EQ(g.inodes_per_block, blksize / 128U, msg);

    sprintf(msg, "%s: inode_blocks", name);
    CHECK_EQ(g.inode_blocks, want_inode_blocks, msg);

    sprintf(msg, "%s: inode_slots", name);
    CHECK_EQ(g.inode_slots, want_slots, msg);

    sprintf(msg, "%s: datablock_start", name);
    CHECK_EQ(g.datablock_start, want_dstart, msg);

    sprintf(msg, "%s: blksize_shift", name);
    CHECK_EQ(g.blksize_shift, want_shift, msg);

    /* Defect 2 */
    sprintf(msg, "%s: total_freeblock", name);
    CHECK_EQ(g.total_freeblock, want_freeblock, msg);

    /* Defect 1 */
    sprintf(msg, "%s: total_freeinode", name);
    CHECK_EQ(g.total_freeinode, want_freeinode, msg);

    sprintf(msg, "%s: nfreeblock", name);
    CHECK_EQ(g.nfreeblock, want_nfreeblock, msg);

    sprintf(msg, "%s: root_block", name);
    CHECK_EQ(g.root_block, want_root_block, msg);

    sprintf(msg, "%s: nfreeinode", name);
    CHECK_EQ(g.nfreeinode, want_nfreeinode, msg);

    /* total_freeinode must always be the physically occupied slots
    ** subtracted from the total -- inode 1 (BALBLK) and inode 2
    ** (root), never a third. */
    sprintf(msg, "%s: free inodes are slots minus two", name);
    CHECK_EQ(g.total_freeinode, g.inode_slots - 2U, msg);

    /* total_freeblock must always account for every data block except
    ** the one the root directory occupies. */
    sprintf(msg, "%s: free blocks are data blocks minus root", name);
    CHECK_EQ(g.total_freeblock,
             g.total_blocks - g.datablock_start - 1U, msg);
}

/* ============================================================
** check_caches
**
** The superblock caches must describe exactly what is on disk: the
** free block cache holds the first data blocks ascending with the
** root block popped off the top, and the free inode cache starts at
** inode 3 because 1 and 2 are taken.
** ============================================================ */
static void
check_caches(void)
{
    UFSFMT_GEOM g;
    unsigned    i;
    int         ok;

    CHECK_EQ(ufsfmt_geometry(&g, 256U, 4096U, 10.0), UFSFMT_GEOM_OK,
             "caches: 256 block disk accepted");

    ok = 1;
    for (i = 0; i < g.nfreeblock; i++)
        if (g.freeblock[i] != g.datablock_start + i) ok = 0;
    CHECK(ok, "free block cache is ascending from datablock_start");

    /* LIFO pop: the root took the highest cached block, and the
    ** popped entry is deliberately left in the array (ufsd-utils does
    ** the same, and a byte comparison would otherwise diverge). */
    CHECK_EQ(g.root_block, g.datablock_start + g.nfreeblock,
             "root block came off the top of the cache");
    CHECK_EQ(g.freeblock[g.nfreeblock], g.root_block,
             "popped cache entry is left in place");

    ok = 1;
    for (i = 0; i < g.nfreeinode; i++)
        if (g.freeinode[i] != i + 3U) ok = 0;
    CHECK(ok, "free inode cache starts at inode 3");

    /* Where the free list stops fitting in the superblock is where
    ** the first chain block has to pick it up. */
    CHECK_EQ(g.chain_start, g.datablock_start + g.sb_cache_blocks,
             "chain starts after the superblock cache");
}

/* ============================================================
** check_rejects
**
** Bad input must be refused, not silently corrected: a disk formatted
** with a block size UFSD cannot address is worse than no disk.
** ============================================================ */
static void
check_rejects(void)
{
    UFSFMT_GEOM g;

    CHECK_EQ(ufsfmt_geometry(&g, 256U, 4000U, 10.0), UFSFMT_GEOM_BLKSIZE,
             "reject: block size not a multiple of 512");
    CHECK_EQ(ufsfmt_geometry(&g, 256U, 256U, 10.0), UFSFMT_GEOM_BLKSIZE,
             "reject: block size below 512");
    CHECK_EQ(ufsfmt_geometry(&g, 256U, 16384U, 10.0), UFSFMT_GEOM_BLKSIZE,
             "reject: block size above 8192");
    CHECK_EQ(ufsfmt_geometry(&g, 256U, 4096U, 0.5), UFSFMT_GEOM_PCT,
             "reject: inode percentage below 1.0");
    CHECK_EQ(ufsfmt_geometry(&g, 256U, 4096U, 75.0), UFSFMT_GEOM_PCT,
             "reject: inode percentage above 50.0");
    CHECK_EQ(ufsfmt_geometry(&g, 4U, 4096U, 10.0), UFSFMT_GEOM_TOOSMALL,
             "reject: fewer than 8 blocks");
    CHECK_EQ(ufsfmt_geometry(&g, 0U, 4096U, 10.0), UFSFMT_GEOM_TOOSMALL,
             "reject: empty dataset");
    CHECK_EQ(ufsfmt_geometry(NULL, 256U, 4096U, 10.0), UFSFMT_GEOM_ARG,
             "reject: NULL geometry");

    /* A rejected call must leave nothing behind that looks usable: a
    ** caller that ignores the return code has to fail loudly, not
    ** format a disk from a half-filled geometry. */
    memset(&g, 0xFF, sizeof(g));
    (void)ufsfmt_geometry(&g, 256U, 4000U, 10.0);
    CHECK_EQ(g.total_blocks, 0U, "reject: geometry is zeroed on failure");
    CHECK_EQ(g.datablock_start, 0U,
             "reject: datablock_start is zeroed on failure");
}

/* ============================================================
** check_planning_table
**
** The SPACE planning table in samplib/ufsfmt promises a maximum file
** count per allocation size.  Those numbers are total_freeinode, so
** they are checked here rather than trusted.
** ============================================================ */
static void
check_planning_table(void)
{
    static const struct {
        unsigned blocks;
        unsigned inode_blocks;
        unsigned max_files;
    } row[] = {
        {    64U,  2U,   62U },
        {   128U,  2U,   62U },
        {   256U,  2U,   62U },
        {   512U,  2U,   62U },
        {  1280U,  4U,  126U },
        {  2560U,  8U,  254U },
        {  5120U, 16U,  510U },
        { 12800U, 40U, 1278U }
    };
    UFSFMT_GEOM g;
    char        msg[96];
    unsigned    i;

    for (i = 0; i < sizeof(row) / sizeof(row[0]); i++) {
        CHECK_EQ(ufsfmt_geometry(&g, row[i].blocks, 4096U, 10.0),
                 UFSFMT_GEOM_OK, "planning table: geometry accepted");

        sprintf(msg, "planning table: %u blocks -> %u inode blocks",
                row[i].blocks, row[i].inode_blocks);
        CHECK_EQ(g.inode_blocks, row[i].inode_blocks, msg);

        sprintf(msg, "planning table: %u blocks -> %u files",
                row[i].blocks, row[i].max_files);
        CHECK_EQ(g.total_freeinode, row[i].max_files, msg);
    }
}

int
main(void)
{
    printf("=== UFSD UFSFMT geometry tests ===\n");

    /* Small -- below 51 data blocks, where the original wrote
    ** total_freeblock = 0.  40 blocks of 4096 is ~160 KB. */
    check_geom("small", 40U, 4096U, 10.0,
               /* inode_blocks */ 2U, /* slots */ 64U, /* dstart */ 4U,
               /* shift */ 12U, /* freeblock */ 35U, /* freeinode */ 62U,
               /* nfreeblock */ 35U, /* root_block */ 39U,
               /* nfreeinode */ 62U);

    /* Standard -- the 1 MB root disk from the documentation. */
    check_geom("standard", 256U, 4096U, 10.0,
               2U, 64U, 4U, 12U, 251U, 62U, 50U, 54U, 62U);

    /* Large -- 10 MB, enough data blocks to need 49 chain blocks. */
    check_geom("large", 2560U, 4096U, 10.0,
               8U, 256U, 10U, 12U, 2549U, 254U, 50U, 60U, 64U);

    /* Legacy block size -- 1 MB at 1024, exercising blksize_shift and
    ** inodes_per_block, and an inode block count that rounds up. */
    check_geom("legacy", 1024U, 1024U, 10.0,
               13U, 104U, 15U, 10U, 1008U, 102U, 50U, 65U, 64U);

    /* Inode share -- 25 percent of a 1 MB disk still lands on the
    ** 2-block minimum, which is worth stating rather than assuming. */
    check_geom("inodes25", 256U, 4096U, 25.0,
               2U, 64U, 4U, 12U, 251U, 62U, 50U, 54U, 62U);

    /* Inode share on a disk large enough for it to matter: 25 percent
    ** of 2560 blocks is 20 inode blocks, not 8. */
    check_geom("inodes25big", 2560U, 4096U, 25.0,
               20U, 640U, 22U, 12U, 2537U, 638U, 50U, 72U, 64U);

    /* Minimum viable disk: 8 blocks of 512. */
    check_geom("minimum", 8U, 512U, 10.0,
               2U, 8U, 4U, 9U, 3U, 6U, 3U, 7U, 6U);

    check_caches();
    check_rejects();
    check_planning_table();

    return mbt_test_summary("TSTUFSG");
}
