/* TSTUFSIN.C - a removed inode goes back to the pool for good (#130)
**
** Host-only.  do_remove, do_rmdir, do_mkdir and do_fopen are static in
** src/ufsd#fil.c, so this test includes the file whole and drives them
** through ufsd_fil_dispatch().  Below the file layer it keeps the real
** src/ufsd#sbl.c and src/ufsd#ino.c, so the free inode cache, the cache
** refill scan and the MODIFY REBUILD scan are the production code.  Only
** the block device (a RAM image), the directory layer and the GFT are
** stand-ins.
**
** The property under test: an inode is free when its on-disk mode is 0.
** The superblock keeps only 64 free inode numbers; every number beyond that
** is dropped on purpose, and the scans bring it back -- but only if the
** inode on disk was cleared.
**
** The image and the scripts are file statics: this is a test, not an AC(1)
** module.
*/

#include <mbtcheck.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/ufsd#fil.c"

/* ============================================================
** RAM disk: 512-byte blocks, 32 ilist blocks (128 inodes: inode 1 is
** reserved and inode 2 is the root, leaving 126), data blocks from 34.
** ============================================================ */

#define BLKSIZE     512U
#define NBLOCKS     300U
#define ILIST_BLKS  32U
#define DATA_START  (UFSD_ILIST_SECTOR + ILIST_BLKS)
#define NINODES     (ILIST_BLKS * (BLKSIZE / UFSD_INODE_SIZE))
#define FREE_INODES (NINODES - 2U)
#define ROOT_BLK    DATA_START

static unsigned char g_img[NBLOCKS][BLKSIZE];

static unsigned g_fail_wsector;         /* ufsd_blk_write fails here (0 = never) */
static unsigned g_dir_add_rc;           /* ufsd_dir_add answers this             */
static unsigned g_watch_ino;            /* inode watched by the sector-1 hook    */
static unsigned g_sb_before_zero;       /* sb written while watched inode nonzero */
static unsigned g_cache_has_ino_at_zero;/* ino was in the cache when zeroed      */

static UFSD_DISK g_disk;

int
ufsd_blk_read(UFSD_DISK *disk, unsigned sector, void *buf)
{
    (void)disk;
    if (sector >= NBLOCKS) return UFSD_RC_IO;
    memcpy(buf, g_img[sector], BLKSIZE);
    return UFSD_RC_OK;
}

static int
watched_inode_is_zero(void)
{
    unsigned char zero[UFSD_INODE_SIZE];
    unsigned      blk = UFSD_ILIST_SECTOR
                        + (g_watch_ino - 1U) / (BLKSIZE / UFSD_INODE_SIZE);
    unsigned      off = ((g_watch_ino - 1U) % (BLKSIZE / UFSD_INODE_SIZE))
                        * UFSD_INODE_SIZE;

    memset(zero, 0, sizeof zero);
    return memcmp(g_img[blk] + off, zero, sizeof zero) == 0;
}

int
ufsd_blk_write(UFSD_DISK *disk, unsigned sector, void *buf)
{
    unsigned i;

    if (sector == g_fail_wsector) return UFSD_RC_IO;
    if (sector >= NBLOCKS) return UFSD_RC_IO;

    if (g_watch_ino != 0U) {
        if (sector == 1U && !watched_inode_is_zero()) g_sb_before_zero++;
        if (sector == UFSD_ILIST_SECTOR
                       + (g_watch_ino - 1U) / (BLKSIZE / UFSD_INODE_SIZE)) {
            for (i = 0; i < disk->sb.nfreeinode; i++)
                if (disk->sb.freeinode[i] == g_watch_ino)
                    g_cache_has_ino_at_zero++;
        }
    }
    memcpy(g_img[sector], buf, BLKSIZE);
    return UFSD_RC_OK;
}

/* ============================================================
** Directory layer: one flat directory, a name table
** ============================================================ */

#define MAXNAMES 160

static struct { char name[UFSD_NAME_MAX + 1]; unsigned ino; } g_names[MAXNAMES];

static int
name_find(const char *name)
{
    int i;

    for (i = 0; i < MAXNAMES; i++)
        if (g_names[i].ino != 0U && strcmp(g_names[i].name, name) == 0)
            return i;
    return -1;
}

unsigned
ufsd_path_lookup(UFSD_DISK *d, unsigned s, const char *p,
                 unsigned *pi, char *o)
{
    const char *base = p[0] == '/' ? p + 1 : p;
    int         i;

    (void)d; (void)s;
    *pi = UFSD_ROOT_INO;
    strcpy(o, base);
    i = name_find(base);
    return i < 0 ? 0U : g_names[i].ino;
}

int
ufsd_dir_add(UFSD_DISK *d, unsigned parent, const char *name, unsigned ino)
{
    int i;

    (void)d; (void)parent;
    if (g_dir_add_rc != UFSD_RC_OK) return (int)g_dir_add_rc;
    for (i = 0; i < MAXNAMES; i++) {
        if (g_names[i].ino == 0U) {
            strcpy(g_names[i].name, name);
            g_names[i].ino = ino;
            return UFSD_RC_OK;
        }
    }
    return UFSD_RC_NOSPACE;
}

int
ufsd_dir_remove(UFSD_DISK *d, unsigned parent, const char *name)
{
    int i = name_find(name);

    (void)d; (void)parent;
    if (i < 0) return UFSD_RC_NOFILE;
    g_names[i].ino = 0U;
    return UFSD_RC_OK;
}

int
ufsd_gft_alloc(UFSD_ANCHOR *a, unsigned *i)
{
    (void)a;
    *i = 0U;
    return UFSD_RC_OK;
}

void ufsd_gft_release(UFSD_ANCHOR *a, unsigned i) { (void)a; (void)i; }
int  ufsd_mount_child(const UFSD_MOUNTPT *t, unsigned n, int p, unsigned i)
{ (void)t; (void)n; (void)p; (void)i; return -1; }
void mtime64(mtime64_t *t) { memset(t, 0, sizeof *t); }
void __64_from_u32(__64 *r, unsigned v) { (void)v; memset(r, 0, sizeof *r); }
void __64_mul_u32(__64 *a, unsigned v, __64 *r) { (void)a; (void)v; (void)r; }
void __64_add_u32(__64 *a, unsigned v, __64 *r) { (void)a; (void)v; (void)r; }
void wtof(const char *text, ...) { (void)text; }

/* ============================================================
** Fixture
** ============================================================ */

static UFSD_STC     g_stc;
static UFSD_ANCHOR  g_anchor;
static UFSD_SESSION g_sess;
static UFSD_UFS     g_ufs;
static UFSD_GFILE   g_gfile;

static void
put_inode(unsigned ino, const UFSD_DINODE *d)
{
    unsigned ipb = BLKSIZE / UFSD_INODE_SIZE;

    memcpy(g_img[UFSD_ILIST_SECTOR + (ino - 1U) / ipb]
           + ((ino - 1U) % ipb) * UFSD_INODE_SIZE, d, UFSD_INODE_SIZE);
}

/* A freshly formatted volume: the root directory in inode 2, every other
** inode free, and an empty free inode cache so the first allocation runs the
** refill scan, as it does after a mount. */
static void
setup(void)
{
    UFSD_DINODE root;
    unsigned    i;

    memset(g_img, 0, sizeof g_img);
    memset(g_names, 0, sizeof g_names);
    memset(&g_disk, 0, sizeof g_disk);
    memset(&g_stc, 0, sizeof g_stc);
    memset(&g_anchor, 0, sizeof g_anchor);
    memset(&g_sess, 0, sizeof g_sess);
    memset(&g_ufs, 0, sizeof g_ufs);
    memset(&g_gfile, 0, sizeof g_gfile);

    g_fail_wsector          = 0;
    g_dir_add_rc            = UFSD_RC_OK;
    g_watch_ino             = 0;
    g_sb_before_zero        = 0;
    g_cache_has_ino_at_zero = 0;

    g_disk.blksize     = BLKSIZE;
    g_disk.mount_mode  = UFSD_MOUNT_RW;
    g_disk.sb.datablock_start   = DATA_START;
    g_disk.sb.volume_size       = NBLOCKS;
    g_disk.sb.inodes_per_block  = BLKSIZE / UFSD_INODE_SIZE;
    g_disk.sb.blksize_shift     = 9U;
    g_disk.sb.ilist_sector      = UFSD_ILIST_SECTOR;

    memset(&root, 0, sizeof root);
    root.mode     = (unsigned short)(UFSD_IFDIR | 0755U);
    root.nlink    = 2;
    root.filesize = 2U * UFSD_DIRENT_SIZE;
    root.addr[0]  = ROOT_BLK;
    put_inode(UFSD_ROOT_INO, &root);

    /* Free block cache: the data blocks after the root's. */
    for (i = 0; i < UFSD_SB_MAX_FREEBLOCK; i++)
        g_disk.sb.freeblock[g_disk.sb.nfreeblock++] = ROOT_BLK + 1U + i;
    g_disk.sb.total_freeblock = NBLOCKS - DATA_START - 1U;

    g_stc.ndisks   = 1;
    g_stc.disks[0] = &g_disk;
    g_anchor.gfiles     = &g_gfile;
    g_anchor.server_stc = &g_stc;
    g_ufs.disk_idx = 0;
    g_ufs.cwd_ino  = UFSD_ROOT_INO;
    g_sess.ufs     = &g_ufs;
    for (i = 0; i < UFSD_MAX_FD; i++)
        g_sess.fd_table[i].gfile_idx = UFSD_FD_UNUSED;
}

static int
request(int func, const char *path, unsigned mode, int with_mode)
{
    UFSREQ   req;
    char     resp[UFSREQ_MAX_INLINE + 16];
    unsigned len;
    int      rc;

    memset(&req, 0, sizeof req);
    req.func = (unsigned short)func;
    if (with_mode) {
        memcpy(req.data, &mode, 4);
        strcpy(req.data + 4, path);
        req.data_len = 4U + (unsigned)strlen(path) + 1U;
    } else {
        strcpy(req.data, path);
        req.data_len = (unsigned)strlen(path) + 1U;
    }
    rc = ufsd_fil_dispatch(&g_anchor, &g_sess, &req, resp, &len);

    if (func == UFSREQ_FOPEN && rc == UFSD_RC_OK) {
        int fd = *(int *)resp;
        g_sess.fd_table[fd].gfile_idx = UFSD_FD_UNUSED;
    }
    return rc;
}

static int
make_file(const char *name)
{
    return request(UFSREQ_FOPEN, name, UFSD_OPEN_WRITE, 1);
}

static int
make_dir(const char *name)
{
    return request(UFSREQ_MKDIR, name, 0, 0);
}

/* Inodes on the image that are free in the sense both scans use. */
static unsigned
count_free_on_disk(void)
{
    UFSD_DINODE d;
    unsigned    ino, n = 0;

    for (ino = 2U; ino <= NINODES; ino++) {
        if (ufsd_ino_read(&g_disk, ino, &d) == UFSD_RC_OK && d.mode == 0)
            n++;
    }
    return n;
}

static void
fill_name(char *out, unsigned i)
{
    sprintf(out, "/n%u", i);
}

/* ============================================================
** Cases
** ============================================================ */

#define BATCH 100U      /* more than the 64 numbers the cache holds */

static void
churn(int dirs)
{
    char     path[16];
    unsigned i, made, gone, again, total;
    int      rc;

    setup();
    rc = make_file("/probe");               /* first allocation: runs the scan */
    CHECK_EQ(rc, UFSD_RC_OK, "first create succeeds");
    rc = request(UFSREQ_REMOVE, "/probe", 0, 0);
    CHECK_EQ(rc, UFSD_RC_OK, "first remove succeeds");
    total = g_disk.sb.total_freeinode;
    CHECK_EQ(total, FREE_INODES, "every inode but 1 and the root is free");

    made = 0;
    for (i = 0; i < BATCH; i++) {
        fill_name(path, i);
        if ((dirs ? make_dir(path) : make_file(path)) == UFSD_RC_OK) made++;
    }
    CHECK_EQ(made, BATCH, "all creates succeed");

    gone = 0;
    for (i = 0; i < BATCH; i++) {
        fill_name(path, i);
        if (request(dirs ? UFSREQ_RMDIR : UFSREQ_REMOVE, path, 0, 0)
            == UFSD_RC_OK) gone++;
    }
    CHECK_EQ(gone, BATCH, "all removes succeed");
    CHECK_EQ(g_disk.sb.total_freeinode, total,
             "FREEINODE total is back at the starting value");
    CHECK_EQ(count_free_on_disk(), FREE_INODES,
             "every freed inode is free on the disk, not only in the cache");

    /* What the operator does: MODIFY REBUILD, then use the whole pool.  Files,
    ** not directories: a directory also takes a block, and the V7 chain
    ** refill reads a stale directory block of this little-endian host as a
    ** chain; that has nothing to do with inodes. */
    rc = ufsd_sb_rebuild_free(&g_disk);
    CHECK_EQ(rc, UFSD_RC_OK, "REBUILD succeeds");
    CHECK_EQ(g_disk.sb.total_freeinode, FREE_INODES,
             "REBUILD finds every freed inode again");

    again = 0;
    for (i = 0; i < FREE_INODES; i++) {
        fill_name(path, i);
        if (make_file(path) == UFSD_RC_OK) again++;
    }
    CHECK_EQ(again, FREE_INODES, "the whole pool can be allocated again");
}

static void
check_files_churn(void)
{
    printf("-- create 100 files, remove them, REBUILD, reuse the pool\n");
    churn(0);
}

static void
check_dirs_churn(void)
{
    printf("-- mkdir 100 directories, rmdir them, REBUILD, reuse the pool\n");
    churn(1);
}

static int
inode_is_zero(unsigned ino)
{
    UFSD_DINODE d, z;

    memset(&z, 0, sizeof z);
    if (ufsd_ino_read(&g_disk, ino, &d) != UFSD_RC_OK) return 0;
    return memcmp(&d, &z, sizeof z) == 0;
}

static void
check_inode_cleared_and_ordered(int dirs)
{
    int rc;
    int i;

    printf("-- %s: inode is cleared on disk before its number is cached\n",
           dirs ? "rmdir" : "remove");
    setup();
    rc = dirs ? make_dir("/x") : make_file("/x");
    CHECK_EQ(rc, UFSD_RC_OK, "create succeeds");
    i = name_find("x");
    g_watch_ino = g_names[i].ino;
    CHECK(!inode_is_zero(g_watch_ino), "the live inode is not zero");

    rc = request(dirs ? UFSREQ_RMDIR : UFSREQ_REMOVE, "/x", 0, 0);
    CHECK_EQ(rc, UFSD_RC_OK, "remove succeeds");
    CHECK(inode_is_zero(g_watch_ino), "the inode is all zero on the disk");
    CHECK_EQ(g_cache_has_ino_at_zero, 0U,
             "the number was not in the cache while the inode was written");
    CHECK_EQ(g_sb_before_zero, 0U,
             "no superblock went to disk before the inode was cleared");
}

static void
check_clear_write_fails(int dirs)
{
    unsigned ino, sector, nfree, total;
    int      rc;

    printf("-- %s: the clearing write fails\n", dirs ? "rmdir" : "remove");
    setup();
    rc = dirs ? make_dir("/x") : make_file("/x");
    CHECK_EQ(rc, UFSD_RC_OK, "create succeeds");
    ino    = g_names[name_find("x")].ino;
    sector = UFSD_ILIST_SECTOR + (ino - 1U) / (BLKSIZE / UFSD_INODE_SIZE);
    nfree  = g_disk.sb.nfreeinode;
    total  = g_disk.sb.total_freeinode;

    g_fail_wsector = sector;
    rc = request(dirs ? UFSREQ_RMDIR : UFSREQ_REMOVE, "/x", 0, 0);
    CHECK_EQ(rc, UFSD_RC_IO, "the write error is the answer");
    CHECK_EQ(g_disk.sb.nfreeinode, nfree,
             "the number is not handed to the cache");
    CHECK_EQ(g_disk.sb.total_freeinode, total, "the free count is unchanged");
}

static void
check_create_rolled_back(int dirs)
{
    int rc;

    printf("-- %s: directory entry cannot be added\n",
           dirs ? "mkdir" : "create");
    setup();
    rc = make_file("/probe");               /* populate the cache */
    CHECK_EQ(rc, UFSD_RC_OK, "probe create succeeds");
    rc = request(UFSREQ_REMOVE, "/probe", 0, 0);
    CHECK_EQ(rc, UFSD_RC_OK, "probe remove succeeds");

    g_dir_add_rc = UFSD_RC_IO;
    rc = dirs ? make_dir("/y") : make_file("/y");
    CHECK_EQ(rc, UFSD_RC_IO, "the directory error is the answer");
    CHECK_EQ(count_free_on_disk(), FREE_INODES,
             "the inode written for it is free on the disk again");
}

int
main(void)
{
    printf("=== UFSD freed inode tests ===\n");

    check_files_churn();
    check_dirs_churn();
    check_inode_cleared_and_ordered(0);
    check_inode_cleared_and_ordered(1);
    check_clear_write_fails(0);
    check_clear_write_fails(1);
    check_create_rolled_back(0);
    check_create_rolled_back(1);

    return mbt_test_summary("TSTUFSIN");
}
