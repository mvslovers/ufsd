/* TSTUFSFR.C - do_fread: who owns the staging buffer on each way out (#134)
**
** Host-only.  do_fread is static in src/ufsd#fil.c, so this test includes the
** file whole and reaches it through ufsd_fil_dispatch(UFSREQ_FREAD).  What it
** replaces is everything below the file layer: ufsd_ino_read and
** ufsd_blk_read are scripted, and malloc/free are wrapped by a table that
** knows every live block, so a leak or a double free is a number.
**
** The ownership rule under test: a 4K read allocates a staging buffer.  On
** success the pointer travels to ufsd_dispatch in resp_data[4..7], which
** copies and frees it.  On any error do_fread returns a bad rc, ufsd_dispatch
** does not look at resp_data, and so do_fread must free the staging buffer
** itself.
**
** The table and the script are file statics: this is a test, not an AC(1)
** module.
*/

#include <mbtcheck.h>
#include <stdlib.h>
#include <string.h>

#define MAXLIVE 8

static void    *g_live[MAXLIVE];
static unsigned g_nlive;                /* blocks malloc'd and not freed    */
static unsigned g_nmalloc;              /* malloc calls since reset         */
static unsigned g_failmalloc;           /* make this malloc call fail (1-based, 0 = never) */
static unsigned g_badfree;              /* free of a block not live         */

static void *
t_malloc(size_t n)
{
    void *p;

    g_nmalloc++;
    if (g_failmalloc != 0U && g_nmalloc == g_failmalloc) return NULL;
    p = malloc(n);
    if (p == NULL) return NULL;
    if (g_nlive >= MAXLIVE) {           /* untracked blocks would hide leaks */
        free(p);
        return NULL;
    }
    g_live[g_nlive++] = p;
    return p;
}

static void
t_free(void *p)
{
    unsigned i;

    if (p == NULL) return;
    for (i = 0; i < g_nlive; i++) {
        if (g_live[i] == p) {
            g_live[i] = g_live[--g_nlive];
            free(p);
            return;
        }
    }
    g_badfree++;
}

#define malloc(n) t_malloc(n)
#define free(p)   t_free(p)

#include "../../src/ufsd#fil.c"

#undef malloc
#undef free

/* ============================================================
** Stand-ins for everything fil.c calls below the file layer
** ============================================================ */

#define BLKSIZE   512U
#define FILESIZE  1000U         /* two blocks: sectors 10 and 11            */
#define SECT_A    10U
#define SECT_B    11U

static int      g_ino_rc;               /* ufsd_ino_read answers this       */
static unsigned g_fail_sector;          /* ufsd_blk_read fails for this one */

int
ufsd_ino_read(UFSD_DISK *disk, unsigned ino, UFSD_DINODE *out)
{
    (void)disk; (void)ino;
    if (g_ino_rc != UFSD_RC_OK) return g_ino_rc;
    memset(out, 0, sizeof *out);
    out->mode     = UFSD_IFREG;
    out->nlink    = 1;
    out->filesize = FILESIZE;
    out->addr[0]  = SECT_A;
    out->addr[1]  = SECT_B;
    return UFSD_RC_OK;
}

int
ufsd_blk_read(UFSD_DISK *disk, unsigned sector, void *buf)
{
    (void)disk;
    if (sector == g_fail_sector) return UFSD_RC_IO;
    memset(buf, (int)('a' + (sector - SECT_A)), BLKSIZE);
    return UFSD_RC_OK;
}

int  ufsd_blk_write(UFSD_DISK *d, unsigned s, void *b)
{ (void)d; (void)s; (void)b; return UFSD_RC_IO; }
int  ufsd_dir_add(UFSD_DISK *d, unsigned i, const char *n, unsigned o)
{ (void)d; (void)i; (void)n; (void)o; return UFSD_RC_IO; }
int  ufsd_dir_remove(UFSD_DISK *d, unsigned i, const char *n)
{ (void)d; (void)i; (void)n; return UFSD_RC_IO; }
int  ufsd_gft_alloc(UFSD_ANCHOR *a, unsigned *i)
{ (void)a; (void)i; return UFSD_RC_IO; }
void ufsd_gft_release(UFSD_ANCHOR *a, unsigned i)
{ (void)a; (void)i; }
int  ufsd_ino_write(UFSD_DISK *d, unsigned i, const UFSD_DINODE *n)
{ (void)d; (void)i; (void)n; return UFSD_RC_IO; }
int  ufsd_mount_child(const UFSD_MOUNTPT *t, unsigned n, int p, unsigned i)
{ (void)t; (void)n; (void)p; (void)i; return -1; }
unsigned ufsd_path_lookup(UFSD_DISK *d, unsigned s, const char *p,
                          unsigned *pi, char *o)
{ (void)d; (void)s; (void)p; (void)pi; (void)o; return 0; }
int  ufsd_sb_alloc_block(UFSD_DISK *d, unsigned *s)
{ (void)d; (void)s; return UFSD_RC_IO; }
int  ufsd_sb_alloc_block_for(UFSD_DISK *d, const UFSD_DINODE *f, unsigned *s)
{ (void)d; (void)f; (void)s; return UFSD_RC_IO; }
int  ufsd_sb_alloc_inode(UFSD_DISK *d, unsigned *i)
{ (void)d; (void)i; return UFSD_RC_IO; }
void ufsd_sb_free_block(UFSD_DISK *d, unsigned s) { (void)d; (void)s; }
void ufsd_sb_free_inode(UFSD_DISK *d, unsigned i) { (void)d; (void)i; }
int  ufsd_sb_write(UFSD_DISK *d) { (void)d; return UFSD_RC_IO; }
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
static UFSBUF  g_pool_buf;              /* what ufsd_dispatch reserved       */
static int     g_reserved;              /* 1: a 4K request carries req->buf  */
static UFSD_ANCHOR  g_anchor;
static UFSD_SESSION g_sess;

static void
setup(void)
{
    unsigned i;

    memset(&g_disk, 0, sizeof g_disk);
    memset(&g_stc, 0, sizeof g_stc);
    memset(&g_gfile, 0, sizeof g_gfile);
    memset(&g_anchor, 0, sizeof g_anchor);
    memset(&g_sess, 0, sizeof g_sess);

    g_disk.blksize     = BLKSIZE;
    g_stc.disks[0]     = &g_disk;
    g_gfile.flags      = UFSD_GF_USED;
    g_gfile.open_mode  = UFSD_OPEN_READ;
    g_gfile.disk_idx   = 0;
    g_gfile.ino        = 7U;
    g_anchor.gfiles    = &g_gfile;
    g_anchor.server_stc = &g_stc;
    for (i = 0; i < UFSD_MAX_FD; i++) g_sess.fd_table[i].gfile_idx = UFSD_FD_UNUSED;
    g_sess.fd_table[0].gfile_idx = 0;

    g_ino_rc      = UFSD_RC_OK;
    g_fail_sector = 0;
    g_nlive       = 0;
    g_nmalloc     = 0;
    g_failmalloc  = 0;
    g_badfree     = 0;
    g_reserved    = 1;
}

typedef struct result {
    int      rc;
    unsigned len;                       /* resp_data_len                    */
    char     resp[UFSREQ_MAX_INLINE + 16];
    unsigned live_at_return;            /* blocks live when dispatch returned */
    int      handed_off;                /* a staging pointer came back      */
    int      stg_was_live;              /* ... and it was a live block      */
    char     stg_data[FILESIZE];        /* copy of the staging buffer       */
} RESULT;

/* Issues the read and then plays ufsd_dispatch: a staging pointer that comes
** back with rc OK is copied and freed here, on every path, so a test never
** leaves a block behind.  What the call itself left live is in the result. */
static void
do_read(unsigned count, RESULT *r)
{
    UFSREQ req;
    int    fd = 0;
    char  *stg;
    unsigned n, i;

    memset(&req, 0, sizeof req);
    memset(r, 0, sizeof *r);
    req.func     = UFSREQ_FREAD;
    req.data_len = 8U;
    if (g_reserved && count > UFSREQ_MAX_INLINE - 4U) req.buf = &g_pool_buf;
    memcpy(req.data, &fd, 4);
    memcpy(req.data + 4, &count, 4);

    r->rc = ufsd_fil_dispatch(&g_anchor, &g_sess, &req, r->resp, &r->len);
    r->live_at_return = g_nlive;

    if (r->rc == UFSD_RC_OK && r->len == 4U && count > UFSREQ_MAX_INLINE - 4U) {
        memcpy(&n, r->resp, 4);
        memcpy(&stg, r->resp + 4, sizeof stg);
        if (stg != NULL) {
            r->handed_off = 1;
            for (i = 0; i < g_nlive; i++)
                if (g_live[i] == (void *)stg) r->stg_was_live = 1;
            if (r->stg_was_live) {
                memcpy(r->stg_data, stg, n < FILESIZE ? n : FILESIZE);
                t_free(stg);
            }
        }
    }
}

/* ============================================================
** Cases
** ============================================================ */

static void
check_inode_read_fails(void)
{
    RESULT r;

    printf("-- 4K read, inode read fails\n");
    setup();
    g_ino_rc = UFSD_RC_IO;
    do_read(4096U, &r);

    CHECK_EQ(r.rc, UFSD_RC_IO, "the inode error is the answer");
    CHECK_EQ(g_nmalloc, 0U, "nothing was allocated before the inode was read");
    CHECK_EQ(r.live_at_return, 0U, "nothing is live");
    CHECK_EQ(g_badfree, 0U, "nothing is freed twice");
}

static void
check_block_malloc_fails(void)
{
    RESULT r;

    printf("-- 4K read, block buffer malloc fails\n");
    setup();
    g_failmalloc = 1U;                  /* 1 = block buffer, 2 = staging   */
    do_read(4096U, &r);

    CHECK_EQ(r.rc, UFSD_RC_IO, "a failed block buffer is an I/O error");
    CHECK_EQ(g_nmalloc, 1U, "only the block buffer was attempted");
    CHECK_EQ(r.live_at_return, 0U, "nothing is live");
    CHECK_EQ(g_badfree, 0U, "nothing is freed twice");
}

static void
check_block_read_fails(void)
{
    RESULT r;

    printf("-- 4K read, device read fails in the loop\n");
    setup();
    g_fail_sector = SECT_B;             /* first block fine, second fails  */
    do_read(4096U, &r);

    CHECK_EQ(r.rc, UFSD_RC_IO, "the read error is the answer");
    CHECK_EQ(r.live_at_return, 0U, "staging and block buffer are both freed");
    CHECK_EQ(g_badfree, 0U, "nothing is freed twice");
}

static void
check_success_hands_off(void)
{
    RESULT r;
    unsigned n;

    printf("-- 4K read, success\n");
    setup();
    do_read(4096U, &r);

    memcpy(&n, r.resp, 4);

    CHECK_EQ(r.rc, UFSD_RC_OK, "rc OK");
    CHECK_EQ(n, FILESIZE, "the whole file is read");
    CHECK_EQ(r.len, 4U, "only the count travels in resp_data");
    CHECK_EQ(r.live_at_return, 1U, "exactly one block is live: the staging buffer");
    CHECK(r.handed_off && r.stg_was_live,
          "resp_data[4..7] names the live staging buffer");
    CHECK(r.stg_data[0] == 'a' && r.stg_data[BLKSIZE - 1U] == 'a'
          && r.stg_data[BLKSIZE] == 'b' && r.stg_data[FILESIZE - 1U] == 'b',
          "the staging buffer holds the file data");
    CHECK_EQ(g_gfile.position, FILESIZE, "the file position advanced");
    CHECK_EQ(g_nlive, 0U, "after the dispatcher frees it nothing is live");
    CHECK_EQ(g_badfree, 0U, "nothing is freed twice");
}

static void
check_inline_read(void)
{
    RESULT r;
    unsigned n;

    printf("-- inline read\n");
    setup();
    do_read(100U, &r);

    memcpy(&n, r.resp, 4);
    CHECK_EQ(r.rc, UFSD_RC_OK, "rc OK");
    CHECK_EQ(n, 100U, "100 bytes read");
    CHECK_EQ(r.len, 104U, "count and data travel in resp_data");
    CHECK(r.resp[4] == 'a' && r.resp[103] == 'a', "the data is inline");
    CHECK_EQ(g_nmalloc, 1U, "only the block buffer was allocated");
    CHECK_EQ(r.live_at_return, 0U, "the block buffer is freed");

    printf("-- inline read, inode read fails\n");
    setup();
    g_ino_rc = UFSD_RC_IO;
    do_read(100U, &r);
    CHECK_EQ(r.rc, UFSD_RC_IO, "the inode error is the answer");
    CHECK_EQ(r.live_at_return, 0U, "nothing is live");
}

static void
check_staging_malloc_fails(void)
{
    RESULT r;
    unsigned n;

    printf("-- 4K read, staging malloc fails: inline fallback\n");
    setup();
    g_failmalloc = 2U;                  /* 1 = block buffer, 2 = staging   */
    do_read(4096U, &r);

    memcpy(&n, r.resp, 4);
    CHECK_EQ(r.rc, UFSD_RC_OK, "rc OK");
    CHECK_EQ(n, UFSREQ_MAX_INLINE - 4U, "clamped to the inline maximum");
    CHECK_EQ(r.len, 4U + n, "the data travels inline");
    CHECK_EQ(r.live_at_return, 0U, "the block buffer is freed");
}

static void
check_no_reserved_buffer(void)
{
    RESULT r;
    unsigned n;

    printf("-- 4K read, CSA pool buffer not reserved: inline from the start\n");
    setup();
    g_reserved = 0;
    do_read(4096U, &r);

    memcpy(&n, r.resp, 4);
    CHECK_EQ(r.rc, UFSD_RC_OK, "rc OK");
    CHECK_EQ(g_nmalloc, 1U, "no staging buffer, only the block buffer");
    CHECK_EQ(n, UFSREQ_MAX_INLINE - 4U, "clamped to the inline maximum");
    CHECK_EQ(r.len, 4U + n, "the data travels inline");
    CHECK(r.resp[4] == 'a' && r.resp[4U + n - 1U] == 'a', "the first bytes of the file");
    CHECK_EQ(g_gfile.position, n, "the position advanced by what was delivered");
    CHECK_EQ(r.live_at_return, 0U, "nothing is live");
    CHECK_EQ(g_badfree, 0U, "nothing is freed twice");
}

int
main(void)
{
    printf("=== UFSD do_fread staging buffer tests ===\n");

    check_inode_read_fails();
    check_block_malloc_fails();
    check_block_read_fails();
    check_success_hands_off();
    check_inline_read();
    check_staging_malloc_fails();
    check_no_reserved_buffer();

    return mbt_test_summary("TSTUFSFR");
}
