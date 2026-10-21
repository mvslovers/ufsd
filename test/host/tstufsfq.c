/* TSTUFSFQ.C - ufsd_dispatch: who frees the FREAD staging buffer (#137)
**
** Host-only.  Includes src/ufsd#que.c whole and calls ufsd_dispatch with a
** UFSREQ_FREAD.  Everything dispatch leans on is a stand-in: the key-0 window
** (__super/__prob), the cross-AS POST, the CSA buffer pool, the session
** lookup, and ufsd_fil_dispatch -- which plays do_fread's side of the
** hand-off protocol from a script.  malloc/free are wrapped by a table of live
** blocks, so a leak or a double free is a number.
**
** The protocol under test (src/ufsd#fil.c, do_fread; #135 for the buffer):
**
**   reservation a FREAD with count > 252 gets a CSA pool buffer in req->buf
**               before ufsd_fil_dispatch runs.  If the pool is empty req->buf
**               stays NULL and do_fread answers inline, clamped to 252.
**               Whatever the outcome, a buffer that does not carry the data
**               goes back to the pool and req->buf is NULL when the client
**               is posted.
**   4K path     resp_data[0..3] = bytes read, resp_data[4..7] = the heap
**               staging buffer, resp_data_len = 4.  Ownership passes to
**               ufsd_dispatch, which copies and frees it -- also for 0 bytes.
**   inline path resp_data_len = 4 + bytes read.  With 0 bytes read that is
**               also 4, and resp_data[4..7] is not written at all.
**
** The second case is why dispatch must not read resp_data[4..7] as a pointer
** unless it knows the buffer was written: the array is on its stack.
** do_fread's own exits are TSTUFSFR (#134).
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
static unsigned g_badfree;              /* free of a block not live         */

static void *
t_malloc(size_t n)
{
    void *p;

    p = malloc(n);
    if (p == NULL) return NULL;
    if (g_nlive >= MAXLIVE) {
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

#include "../../src/ufsd#que.c"

#undef malloc
#undef free

/* ============================================================
** Stand-ins for everything que.c calls
** ============================================================ */

static UFSBUF   g_pool_buf;             /* the one CSA pool buffer          */
static int      g_pool_empty;           /* ufsd_buf_alloc finds nothing     */
static unsigned g_allocs;               /* ufsd_buf_alloc calls             */
static int      g_buf_seen;             /* req->buf set when do_fread runs  */
static unsigned g_buf_out;              /* pool buffers handed out, not returned */
static unsigned g_posts;                /* client ECBs posted               */

int
__super(unsigned char pswkey, unsigned char *savekey)
{
    (void)pswkey;
    *savekey = 0x80;
    return 0;
}

int
__prob(unsigned char pswkey, unsigned char *savekey)
{
    (void)pswkey; (void)savekey;
    return 0;
}

void
__xmpost(void *ascb, void *ecb, unsigned postcode)
{
    (void)ascb; (void)ecb; (void)postcode;
    g_posts++;
}

void
wtof(const char *text, ...)
{
    (void)text;
}

void
ufsd_trace(UFSD_ANCHOR *anchor, unsigned short func, unsigned token,
           unsigned short rc)
{
    (void)anchor; (void)func; (void)token; (void)rc;
}

UFSBUF *
ufsd_buf_alloc(UFSD_ANCHOR *anchor)
{
    (void)anchor;
    g_allocs++;
    if (g_pool_empty) return NULL;
    g_buf_out++;
    return &g_pool_buf;
}

void
ufsd_buf_free(UFSD_ANCHOR *anchor, UFSBUF *buf)
{
    (void)anchor;
    if (buf != &g_pool_buf || g_buf_out == 0) {
        g_badfree++;
        return;
    }
    g_buf_out--;
}

static UFSD_SESSION g_sess;

UFSD_SESSION *
ufsd_sess_find(UFSD_ANCHOR *anchor, unsigned token)
{
    (void)anchor; (void)token;
    return &g_sess;
}

int
ufsd_sess_open(UFSD_ANCHOR *anchor, UFSREQ *req, unsigned *token)
{
    (void)anchor; (void)req; (void)token;
    return UFSD_RC_NOTIMPL;
}

int
ufsd_sess_close(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    (void)anchor; (void)req;
    return UFSD_RC_NOTIMPL;
}

int
ufsd_sess_setuser(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    (void)anchor; (void)req;
    return UFSD_RC_NOTIMPL;
}

/* The script for ufsd_fil_dispatch: what do_fread leaves behind. */

#define MODE_4K     0   /* staging buffer, bread bytes of 0x5A in it        */
#define MODE_INLINE 1   /* data inline in resp_data, resp_data[4..7] if any */
#define MODE_ERROR  2   /* rc != OK, nothing handed over                    */

static int      g_mode;
static unsigned g_bread;
static int      g_rc;

int
ufsd_fil_dispatch(UFSD_ANCHOR *anchor, UFSD_SESSION *sess, UFSREQ *req,
                  char *resp_data, unsigned *resp_data_len)
{
    (void)anchor; (void)sess;

    g_buf_seen = (req->buf != NULL);
    if (g_mode == MODE_ERROR) {
        *resp_data_len = 0;
        return g_rc;
    }
    *(unsigned *)resp_data = g_bread;
    if (g_mode == MODE_4K) {
        char *staging;

        staging = (char *)t_malloc(4096U);
        if (staging == NULL) return UFSD_RC_IO;
        memset(staging, 0x5A, g_bread);
        *(char **)(resp_data + 4) = staging;
        *resp_data_len = 4U;
    } else {
        memset(resp_data + 4, 0x5A, g_bread);
        *resp_data_len = 4U + g_bread;
    }
    return g_rc;
}

/* ============================================================
** The fixture
** ============================================================ */

static UFSD_ANCHOR g_anchor;
static UFSREQ      g_req;

/* Dirty the stack that ufsd_dispatch's resp_data[] is about to occupy, so a
** read of resp_data[4..7] that nobody wrote is not a lucky zero. */
static void __attribute__((noinline))
dirty_stack(void)
{
    volatile char junk[2048];
    unsigned      i;

    for (i = 0; i < sizeof junk; i++) junk[i] = (char)0xA5;
}

static void
run_fread(int mode, unsigned bread, int rc, unsigned count)
{
    g_mode  = mode;
    g_bread = bread;
    g_rc    = rc;

    memset(&g_req, 0, sizeof g_req);
    memcpy(g_req.eye, "UFSREQ__", 8);
    g_req.func          = UFSREQ_FREAD;
    g_req.session_token = 1U;
    g_req.buf           = NULL;
    g_req.data_len      = 8U;
    *(unsigned *)(g_req.data + 4) = count;
    g_allocs            = 0;
    g_buf_seen          = 0;
    g_buf_out           = 0;
    g_posts             = 0;
    g_nlive             = 0;
    g_badfree           = 0;

    dirty_stack();
    ufsd_dispatch(&g_anchor, &g_req);
}

/* ============================================================
** Cases
** ============================================================ */

static void
test_4k_eof_frees_staging(void)
{
    run_fread(MODE_4K, 0U, UFSD_RC_OK, 4096U);

    CHECK_EQ(g_req.rc, UFSD_RC_OK, "4K EOF: rc OK");
    CHECK_EQ(g_req.data_len, 4U, "4K EOF: the count only");
    CHECK_EQ(*(unsigned *)g_req.data, 0U, "4K EOF: zero bytes read");
    CHECK(g_req.buf == NULL, "4K EOF: no pool buffer left in the reply");
    CHECK_EQ(g_buf_out, 0U, "4K EOF: the reserved pool buffer is back");
    CHECK_EQ(g_posts, 1U, "4K EOF: client posted once");
    CHECK_EQ(g_nlive, 0U, "4K EOF: staging buffer freed");
    CHECK_EQ(g_badfree, 0U, "4K EOF: no free of a foreign block");
}

static void
test_4k_data_unchanged(void)
{
    run_fread(MODE_4K, 300U, UFSD_RC_OK, 4096U);

    CHECK_EQ(g_req.rc, UFSD_RC_OK, "4K data: rc OK");
    CHECK_EQ(g_req.data_len, 4U, "4K data: the count in data[]");
    CHECK_EQ(*(unsigned *)g_req.data, 300U, "4K data: 300 bytes read");
    CHECK(g_req.buf == &g_pool_buf, "4K data: answer in the pool buffer");
    CHECK(g_pool_buf.data[0] == 0x5A && g_pool_buf.data[299] == 0x5A,
          "4K data: bytes copied to the pool buffer");
    CHECK(g_buf_seen, "4K data: the buffer was reserved before do_fread");
    CHECK_EQ(g_allocs, 1U, "4K data: one pool buffer taken");
    CHECK_EQ(g_buf_out, 1U, "4K data: the router returns it");
    CHECK_EQ(g_posts, 1U, "4K data: client posted once");
    CHECK_EQ(g_nlive, 0U, "4K data: staging buffer freed once");
    CHECK_EQ(g_badfree, 0U, "4K data: no double free");
}

static void
test_inline_eof_frees_nothing(void)
{
    run_fread(MODE_INLINE, 0U, UFSD_RC_OK, 100U);

    CHECK_EQ(g_req.rc, UFSD_RC_OK, "inline EOF: rc OK");
    CHECK_EQ(g_req.data_len, 4U, "inline EOF: the count only");
    CHECK_EQ(*(unsigned *)g_req.data, 0U, "inline EOF: zero bytes read");
    CHECK(g_req.buf == NULL, "inline EOF: no pool buffer");
    CHECK_EQ(g_posts, 1U, "inline EOF: client posted once");
    CHECK_EQ(g_badfree, 0U, "inline EOF: resp_data[4..7] not taken for a pointer");
}

static void
test_inline_data_unchanged(void)
{
    run_fread(MODE_INLINE, 100U, UFSD_RC_OK, 100U);

    CHECK_EQ(g_req.rc, UFSD_RC_OK, "inline data: rc OK");
    CHECK_EQ(g_req.data_len, 104U, "inline data: count + data");
    CHECK_EQ(*(unsigned *)g_req.data, 100U, "inline data: 100 bytes read");
    CHECK(g_req.data[4] == 0x5A && g_req.data[103] == 0x5A,
          "inline data: bytes copied");
    CHECK(g_req.buf == NULL, "inline data: no pool buffer");
    CHECK_EQ(g_allocs, 0U, "inline data: no pool buffer taken");
    CHECK_EQ(g_badfree, 0U, "inline data: nothing freed");
}

static void
test_4k_error_returns_buffer(void)
{
    run_fread(MODE_ERROR, 0U, UFSD_RC_IO, 4096U);

    CHECK_EQ(g_req.rc, UFSD_RC_IO, "4K error: rc passed on");
    CHECK(g_req.buf == NULL, "4K error: no pool buffer in the reply");
    CHECK_EQ(g_buf_out, 0U, "4K error: the reserved pool buffer is back");
    CHECK_EQ(g_badfree, 0U, "4K error: no foreign free");
}

static void
test_4k_inline_answer_returns_buffer(void)
{
    /* staging malloc failed in do_fread: 252 bytes inline */
    run_fread(MODE_INLINE, 252U, UFSD_RC_OK, 4096U);

    CHECK_EQ(g_req.data_len, 256U, "4K, inline answer: count + 252 bytes");
    CHECK_EQ(*(unsigned *)g_req.data, 252U, "4K, inline answer: 252 bytes read");
    CHECK(g_req.buf == NULL, "4K, inline answer: no pool buffer in the reply");
    CHECK_EQ(g_buf_out, 0U, "4K, inline answer: the reserved pool buffer is back");
    CHECK_EQ(g_badfree, 0U, "4K, inline answer: no foreign free");
}

static void
test_pool_empty_answers_inline(void)
{
    g_pool_empty = 1;
    run_fread(MODE_INLINE, 252U, UFSD_RC_OK, 4096U);
    g_pool_empty = 0;

    CHECK_EQ(g_allocs, 1U, "pool empty: a buffer was asked for");
    CHECK(!g_buf_seen, "pool empty: do_fread sees no buffer");
    CHECK_EQ(g_req.rc, UFSD_RC_OK, "pool empty: rc OK");
    CHECK_EQ(g_req.data_len, 256U, "pool empty: count + 252 bytes inline");
    CHECK(g_req.buf == NULL, "pool empty: no pool buffer in the reply");
    CHECK_EQ(g_badfree, 0U, "pool empty: nothing freed");
    CHECK_EQ(g_posts, 1U, "pool empty: client posted once");
}

int
main(void)
{
    test_4k_eof_frees_staging();
    test_4k_data_unchanged();
    test_inline_eof_frees_nothing();
    test_inline_data_unchanged();
    test_4k_error_returns_buffer();
    test_4k_inline_answer_returns_buffer();
    test_pool_empty_answers_inline();
    return mbt_test_summary("TSTUFSFQ");
}
