/* TSTUFSFW.C - request lengths are checked against what the request carries (#150)
**
** Host-only.  do_fwrite and the path functions are static in src/ufsd#fil.c,
** so this test includes the file whole and drives them through
** ufsd_fil_dispatch().  What it replaces is everything below the file layer:
** the inode, the block device and the path lookup are scripted, and they
** count what reaches them.
**
** The property under test: a count or a path taken from the request is
** compared with what the request really carries before anything is read from
** it or written to the file.  A rejected request writes nothing, answers
** UFSD_RC_INVALID and, for FWRITE, a byte count of 0.
**
** The counters are file statics: this is a test, not an AC(1) module.
*/

#include <mbtcheck.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/ufsd#fil.c"

#define BLKSIZE   512U
#define NSECT     160U
#define DATA_BASE 100U          /* ufsd_sb_alloc_block hands out 100, 101, ... */

static unsigned char g_img[NSECT][BLKSIZE];
static unsigned g_nblkwrite;            /* ufsd_blk_write calls                */
static unsigned g_ninowrite;            /* ufsd_ino_write calls                */
static unsigned g_nlookup;              /* ufsd_path_lookup calls              */
static unsigned g_maxblkwrite;          /* ufsd_blk_write fails after this many */
static unsigned g_nalloc;               /* blocks handed out                   */
static unsigned g_filesize_written;     /* filesize of the last ino_write      */

int
ufsd_ino_read(UFSD_DISK *disk, unsigned ino, UFSD_DINODE *out)
{
    (void)disk; (void)ino;
    memset(out, 0, sizeof *out);
    out->mode  = UFSD_IFREG;
    out->nlink = 1;
    return UFSD_RC_OK;
}

int
ufsd_ino_write(UFSD_DISK *d, unsigned i, const UFSD_DINODE *n)
{
    (void)d; (void)i;
    g_ninowrite++;
    g_filesize_written = n->filesize;
    return UFSD_RC_OK;
}

int
ufsd_blk_read(UFSD_DISK *disk, unsigned sector, void *buf)
{
    (void)disk;
    if (sector >= NSECT) return UFSD_RC_IO;
    memcpy(buf, g_img[sector], BLKSIZE);
    return UFSD_RC_OK;
}

int
ufsd_blk_write(UFSD_DISK *disk, unsigned sector, void *buf)
{
    (void)disk;
    g_nblkwrite++;
    if (g_nblkwrite > g_maxblkwrite) return UFSD_RC_IO;  /* stops a runaway */
    if (sector >= NSECT) return UFSD_RC_IO;
    memcpy(g_img[sector], buf, BLKSIZE);
    return UFSD_RC_OK;
}

int
ufsd_sb_alloc_block(UFSD_DISK *d, unsigned *s)
{
    (void)d;
    *s = DATA_BASE + g_nalloc++;
    return UFSD_RC_OK;
}

unsigned
ufsd_path_lookup(UFSD_DISK *d, unsigned s, const char *p,
                 unsigned *pi, char *o)
{
    (void)d; (void)s; (void)p; (void)pi; (void)o;
    g_nlookup++;
    return 0;
}

int  ufsd_dir_add(UFSD_DISK *d, unsigned i, const char *n, unsigned o)
{ (void)d; (void)i; (void)n; (void)o; return UFSD_RC_IO; }
int  ufsd_dir_remove(UFSD_DISK *d, unsigned i, const char *n)
{ (void)d; (void)i; (void)n; return UFSD_RC_IO; }
int  ufsd_gft_alloc(UFSD_ANCHOR *a, unsigned *i)
{ (void)a; (void)i; return UFSD_RC_IO; }
void ufsd_gft_release(UFSD_ANCHOR *a, unsigned i)
{ (void)a; (void)i; }
int  ufsd_mount_child(const UFSD_MOUNTPT *t, unsigned n, int p, unsigned i)
{ (void)t; (void)n; (void)p; (void)i; return -1; }
int  ufsd_sb_alloc_inode(UFSD_DISK *d, unsigned *i)
{ (void)d; (void)i; return UFSD_RC_IO; }
void ufsd_sb_free_block(UFSD_DISK *d, unsigned s) { (void)d; (void)s; }
void ufsd_sb_free_inode(UFSD_DISK *d, unsigned i) { (void)d; (void)i; }
int  ufsd_sb_write(UFSD_DISK *d) { (void)d; return UFSD_RC_OK; }
void mtime64(mtime64_t *t) { memset(t, 0, sizeof *t); }
void __64_from_u32(__64 *r, unsigned v) { (void)v; memset(r, 0, sizeof *r); }
void __64_mul_u32(__64 *a, unsigned v, __64 *r) { (void)a; (void)v; (void)r; }
void __64_add_u32(__64 *a, unsigned v, __64 *r) { (void)a; (void)v; (void)r; }

/* ============================================================
** Fixture
** ============================================================ */

static UFSD_DISK    g_disk;
static UFSD_STC     g_stc;
static UFSD_GFILE   g_gfile;
static UFSD_ANCHOR  g_anchor;
static UFSD_SESSION g_sess;
static UFSD_UFS     g_ufs;
static UFSBUF       g_pool_buf;

static void
setup(void)
{
    unsigned i;

    memset(g_img, 0, sizeof g_img);
    memset(&g_disk, 0, sizeof g_disk);
    memset(&g_stc, 0, sizeof g_stc);
    memset(&g_gfile, 0, sizeof g_gfile);
    memset(&g_anchor, 0, sizeof g_anchor);
    memset(&g_sess, 0, sizeof g_sess);
    memset(&g_ufs, 0, sizeof g_ufs);
    memset(&g_pool_buf, 'P', sizeof g_pool_buf);

    g_disk.blksize     = BLKSIZE;
    g_disk.mount_mode  = UFSD_MOUNT_RW;
    g_stc.disks[0]     = &g_disk;
    g_stc.ndisks       = 1;
    g_gfile.flags      = UFSD_GF_USED;
    g_gfile.open_mode  = UFSD_OPEN_WRITE;
    g_gfile.disk_idx   = 0;
    g_gfile.ino        = 7U;
    g_anchor.gfiles    = &g_gfile;
    g_anchor.server_stc = &g_stc;
    g_ufs.disk_idx     = 0;
    g_ufs.cwd_ino      = UFSD_ROOT_INO;
    g_sess.ufs         = &g_ufs;
    for (i = 0; i < UFSD_MAX_FD; i++) g_sess.fd_table[i].gfile_idx = UFSD_FD_UNUSED;
    g_sess.fd_table[0].gfile_idx = 0;

    g_nblkwrite = g_ninowrite = g_nlookup = g_nalloc = 0;
    g_filesize_written = 0;
    g_maxblkwrite = 8U;
}

typedef struct result {
    int      rc;
    unsigned len;                       /* resp_data_len                       */
    unsigned written;                   /* resp_data[0..3]                     */
    char     resp[UFSREQ_MAX_INLINE + 16];
} RESULT;

/* One FWRITE.  'count' is the field on the wire, 'data_len' what the request
** carries; with_buf plays the router that copied the client's buffer to the
** pool (data_len is then 8, the data sits in g_pool_buf). */
static void
do_write(unsigned count, unsigned data_len, int with_buf, RESULT *r)
{
    UFSREQ req;
    int    fd = 0;

    memset(&req, 'x', sizeof req);      /* what a recycled block holds        */
    memset(r, 0, sizeof *r);
    req.func     = UFSREQ_FWRITE;
    req.data_len = data_len;
    req.buf      = with_buf ? &g_pool_buf : NULL;
    memcpy(req.data, &fd, 4);
    memcpy(req.data + 4, &count, 4);
    memset(r->resp, 0x7f, sizeof r->resp);

    r->rc = ufsd_fil_dispatch(&g_anchor, &g_sess, &req, r->resp, &r->len);
    memcpy(&r->written, r->resp, 4);
}

static void
check_rejected(const char *what, unsigned count, unsigned data_len, int with_buf)
{
    RESULT r;

    printf("-- FWRITE %s: count=%08X data_len=%u buf=%d\n", what, count,
           data_len, with_buf);
    setup();
    do_write(count, data_len, with_buf, &r);

    CHECK_EQ(r.rc, UFSD_RC_INVALID, "rejected");
    CHECK_EQ(r.len, 4U, "answers with a byte count");
    CHECK_EQ(r.written, 0U, "0 bytes written");
    CHECK_EQ(g_nblkwrite, 0U, "no block written");
    CHECK_EQ(g_ninowrite, 0U, "inode untouched");
    CHECK_EQ(g_gfile.position, 0U, "position unchanged");
}

/* ============================================================
** FWRITE cases
** ============================================================ */

static void
check_large_inline(void)
{
    /* counts at the top of the unsigned range */
    check_rejected("large count", 0xFFFFFFF8U, 8U, 0);
    check_rejected("large count", 0xFFFFFFF8U, 16U, 0);
    check_rejected("large count", 0xFFFFFFF9U, 16U, 0);
    check_rejected("large count", 0xFFFFFFFFU, 8U, 0);
    check_rejected("large count", 0xFFFFFFFFU, UFSREQ_MAX_INLINE, 0);
    check_rejected("huge count", 0x80000000U, UFSREQ_MAX_INLINE, 0);
}

static void
check_large_buffer(void)
{
    check_rejected("large count", 0xFFFFFFF8U, 8U, 1);
    check_rejected("large count", 0xFFFFFFFFU, 8U, 1);
    check_rejected("one past the buffer", 4097U, 8U, 1);
}

static void
check_inline_boundary(void)
{
    RESULT r;

    printf("-- FWRITE inline, data_len - 8 bytes exactly\n");
    setup();
    do_write(10U, 18U, 0, &r);
    CHECK_EQ(r.rc, UFSD_RC_OK, "rc OK");
    CHECK_EQ(r.written, 10U, "10 bytes written");
    CHECK_EQ(g_gfile.position, 10U, "position advanced");
    CHECK_EQ(g_nblkwrite, 1U, "one block written");
    CHECK(g_img[DATA_BASE][0] == 'x' && g_img[DATA_BASE][9] == 'x'
          && g_img[DATA_BASE][10] == 0, "exactly the inline bytes landed");

    check_rejected("one more than the request carries", 11U, 18U, 0);

    printf("-- FWRITE inline, largest request\n");
    setup();
    do_write(UFSREQ_MAX_INLINE - 8U, UFSREQ_MAX_INLINE, 0, &r);
    CHECK_EQ(r.rc, UFSD_RC_OK, "rc OK");
    CHECK_EQ(r.written, UFSREQ_MAX_INLINE - 8U, "all inline bytes written");

    check_rejected("one past the largest request", UFSREQ_MAX_INLINE - 7U,
                   UFSREQ_MAX_INLINE, 0);
    check_rejected("data_len beyond the block", UFSREQ_MAX_INLINE - 7U,
                   UFSREQ_MAX_INLINE + 64U, 0);
}

static void
check_buffer_boundary(void)
{
    RESULT r;

    printf("-- FWRITE from the pool buffer, 4096 bytes\n");
    setup();
    do_write(4096U, 8U, 1, &r);
    CHECK_EQ(r.rc, UFSD_RC_OK, "rc OK");
    CHECK_EQ(r.written, 4096U, "4096 bytes written");
    CHECK_EQ(g_nblkwrite, 8U, "eight blocks written");
    CHECK_EQ(g_img[DATA_BASE + 7U][BLKSIZE - 1U], (unsigned char)'P',
             "the pool buffer's last byte landed");
    CHECK_EQ(g_filesize_written, 4096U, "filesize follows");
}

static void
check_zero_and_short(void)
{
    RESULT r;

    printf("-- FWRITE count 0\n");
    setup();
    do_write(0U, 8U, 0, &r);
    CHECK_EQ(r.rc, UFSD_RC_OK, "rc OK");
    CHECK_EQ(r.written, 0U, "nothing written");

    printf("-- FWRITE request shorter than fd + count\n");
    setup();
    do_write(1U, 7U, 0, &r);
    CHECK_EQ(r.rc, UFSD_RC_INVALID, "rejected");
    CHECK_EQ(r.written, 0U, "0 bytes written");
}

/* ============================================================
** Path functions: the string must end inside the request
** ============================================================ */

static int
do_path(unsigned func, unsigned off, unsigned data_len, int terminated)
{
    UFSREQ   req;
    RESULT   r;
    unsigned mode = UFSD_OPEN_READ;

    memset(&req, 'a', sizeof req);
    req.func     = func;
    req.buf      = NULL;
    req.data_len = data_len;
    if (off == 4U) memcpy(req.data, &mode, 4);
    req.data[off] = '/';                /* absolute: no cwd involved           */
    if (terminated)
        req.data[data_len - 1U] = '\0';
    else if (data_len < UFSREQ_MAX_INLINE)
        req.data[data_len] = '\0';      /* stale byte just past the request    */

    memset(&r, 0, sizeof r);
    return ufsd_fil_dispatch(&g_anchor, &g_sess, &req, r.resp, &r.len);
}

static void
check_path_func(const char *name, unsigned func, unsigned off)
{
    int rc;

    printf("-- %s: path without a NUL inside the request\n", name);
    setup();
    rc = do_path(func, off, UFSREQ_MAX_INLINE, 0);
    CHECK_EQ(rc, UFSD_RC_INVALID, "rejected");
    CHECK_EQ(g_nlookup, 0U, "the path is never walked");

    setup();
    rc = do_path(func, off, 40U, 0);
    CHECK_EQ(rc, UFSD_RC_INVALID, "rejected (short request, no NUL)");
    CHECK_EQ(g_nlookup, 0U, "the path is never walked");

    printf("-- %s: terminated path\n", name);
    setup();
    rc = do_path(func, off, UFSREQ_MAX_INLINE, 1);
    CHECK(rc != UFSD_RC_INVALID, "a NUL in the last byte is accepted");
    CHECK_EQ(g_nlookup, 1U, "the path is looked up");

    setup();
    rc = do_path(func, off, off + 3U, 1);
    CHECK(rc != UFSD_RC_INVALID, "a short terminated path is accepted");
    CHECK_EQ(g_nlookup, 1U, "the path is looked up");
}

static void
check_paths(void)
{
    check_path_func("MKDIR", UFSREQ_MKDIR, 0U);
    check_path_func("RMDIR", UFSREQ_RMDIR, 0U);
    check_path_func("CHGDIR", UFSREQ_CHGDIR, 0U);
    check_path_func("REMOVE", UFSREQ_REMOVE, 0U);
    check_path_func("DIROPEN", UFSREQ_DIROPEN, 0U);
    check_path_func("STAT", UFSREQ_STAT, 0U);
    check_path_func("FOPEN", UFSREQ_FOPEN, 4U);
}

int
main(void)
{
    printf("=== UFSD request length checks ===\n");

    check_large_inline();
    check_large_buffer();
    check_inline_boundary();
    check_buffer_boundary();
    check_zero_and_short();
    check_paths();

    return mbt_test_summary("TSTUFSFW");
}
