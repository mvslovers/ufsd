/* UFSFMT.H - UFS370 format geometry
**
** Deliberately free of MVS dependencies: this header and its
** implementation (src/ufsfmtg.c) contain only integer arithmetic over
** the on-disk geometry, so the same source compiles for cc370 and for
** a host compiler.  That is what makes the counter arithmetic -- the
** part of #50 that had three defects -- testable without an MVS
** round-trip (test/mvs/tstufsg.c, `make test-host`).
**
** Everything that touches an actual disk, an EBCDIC string or a
** control block lives in src/ufsfmt.c and is target-only.  In
** particular no struct here is ever written to disk: the on-disk
** images are built from UFSD_SB / UFSD_DINODE / UFSD_DIRENT
** (include/ufsd.h) on MVS, where the byte order is the disk's.
*/

#ifndef UFSFMT_H
#define UFSFMT_H

/* On-disk constants (mirror of ufsd.h; repeated so this header stays
** free of MVS includes).  ufsfmt_geometry() asserts they agree when
** compiled on MVS -- see the compile-time check in src/ufsfmt.c. */
#define UFSFMT_ILIST_SECTOR     2U    /* ilist starts at sector 2         */
#define UFSFMT_ROOT_INO         2U    /* root directory inode             */
#define UFSFMT_BALBLK_INO       1U    /* monument inode, never allocated  */
#define UFSFMT_INODE_SIZE     128U    /* on-disk inode size               */
#define UFSFMT_DIRENT_SIZE     64U    /* on-disk directory entry size     */
#define UFSFMT_MAX_FREEBLOCK   51U    /* free block cache in superblock   */
#define UFSFMT_MAX_FREEINODE   64U    /* free inode cache in superblock   */

#define UFSFMT_MIN_BLKSIZE    512U
#define UFSFMT_MAX_BLKSIZE   8192U
#define UFSFMT_MIN_BLOCKS       8U    /* boot + super + 2 ilist + data    */

#define UFSFMT_MIN_INODEPCT   1.0
#define UFSFMT_MAX_INODEPCT  50.0

/* ============================================================
** UFSFMT_GEOM
**
** Everything the format needs to know about a disk of a given size,
** derived once and then only read.  The free block/inode caches are
** the exact contents the superblock carries after the root directory
** has been created, so the writer never recomputes a counter.
** ============================================================ */
typedef struct ufsfmt_geom UFSFMT_GEOM;
struct ufsfmt_geom {
    unsigned blksize;             /* bytes per block                     */
    unsigned total_blocks;        /* volume size in blocks               */
    unsigned inodes_per_block;    /* blksize / 128                       */
    unsigned inode_blocks;        /* ilist length in blocks              */
    unsigned inode_slots;         /* inode_blocks * inodes_per_block     */
    unsigned datablock_start;     /* first data sector                   */
    unsigned blksize_shift;       /* log2(blksize)                       */

    unsigned total_freeblock;     /* free data blocks after root created */
    unsigned total_freeinode;     /* free inode slots after root created */

    unsigned sb_cache_blocks;     /* blocks seeded into freeblock[]      */
    unsigned nfreeblock;          /* live entries after the root pop     */
    unsigned freeblock[UFSFMT_MAX_FREEBLOCK];

    unsigned root_block;          /* data block holding "." and ".."     */

    unsigned nfreeinode;          /* live entries in freeinode[]         */
    unsigned freeinode[UFSFMT_MAX_FREEINODE];

    unsigned chain_start;         /* first block covered by a chain block
                                  ** (== total_blocks when none needed)  */
};

/* ufsfmt_geometry return codes */
#define UFSFMT_GEOM_OK        0
#define UFSFMT_GEOM_ARG       1   /* NULL geom                           */
#define UFSFMT_GEOM_BLKSIZE   2   /* blksize out of range / not mult 512 */
#define UFSFMT_GEOM_PCT       3   /* inode percentage out of range       */
#define UFSFMT_GEOM_TOOSMALL  4   /* not enough blocks for a filesystem  */

/* ============================================================
** ufsfmt_geometry
**
** Derive the complete geometry of a filesystem of total_blocks
** blocks of blksize bytes, reserving inode_pct percent for inodes.
** Fills *g and returns UFSFMT_GEOM_OK, or leaves *g zeroed and
** returns one of the codes above.
** ============================================================ */
int ufsfmt_geometry(UFSFMT_GEOM *g, unsigned total_blocks,
                    unsigned blksize, double inode_pct);

/* ============================================================
** Reporting an owner that may be absent (#62)
**
** UFSFMT leaves owner and group empty unless OWNER/GROUP say
** otherwise, so both the summary and the suggested parmlib line have
** to render "no owner" as something other than nothing.  Both are
** plain string work over ASCII-safe character literals, which is why
** they sit here with the geometry and not in the target-only half.
** ============================================================ */

/* What the report prints where an owner would be. */
#define UFSFMT_UNOWNED  "(none)"

/* Returns owner, or UFSFMT_UNOWNED when it is empty or NULL. */
const char *ufsfmt_owner_text(const char *owner);

/* ============================================================
** ufsfmt_mount_stmt
**
** Build the parmlib MOUNT statement the report suggests, into out
** (at most outsz bytes including the NUL, always terminated).
**
** The OWNER keyword is omitted entirely when there is no owner:
** `OWNER()` is a syntax error to the parmlib parser, and an operator
** copying the suggested line has no reason to doubt it.  A mount
** without OWNER is also what an unowned filesystem means -- any
** authenticated user may write to it.
** ============================================================ */
void ufsfmt_mount_stmt(char *out, unsigned outsz,
                       const char *dsn, const char *owner);

#endif /* UFSFMT_H */
