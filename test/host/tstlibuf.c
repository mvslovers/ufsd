/* TSTLIBUF.C - libufs client library, driven through a stubbed SSI
**
** Host-only: builds client/libufs.c unchanged and satisfies its one external
** MVS call, iefssreq() (SVC 34), with the function below.  Every request
** libufs issues lands in that stub, which records it and then answers from a
** script -- so a failure that needs a live subsystem on a live MVS (the
** transport failing, the router refusing, the server answering short) is a
** line in a test instead.
**
** The stub can reproduce, per call:
**
**   transport failure   iefssreq returns R15 != 0, nothing else is touched
**   router refusal      R15 = 0, SSOBRETN != 0, the UFSSSOB left exactly as
**                       the client built it (what ufsd#ssi.c does on its
**                       early returns)
**   server error        R15 = 0, ufsssob.rc != 0
**   short / no answer   R15 = 0, rc = 0, data_len smaller than the caller
**                       needs -- or the response area not written at all
**
** The script and the request log are file statics: this is a test, not an
** AC(1) module.  libufs.c itself gains none.
**
** libufs turns SSOBRETN != 0 with an unwritten response into the router's
** code as the request's rc (test_error_propagation).
**
** Add a case: script the answers with the s_*() helpers, call the libufs
** entry point, then check the result, EXPECT_CALLS() and g_seen[].
*/

#include <mbtcheck.h>
#include <string.h>

#include "libufs.h"
#include "ufsd.h"
#include <ibm/mvs/iefssobh.h>
#include <ibm/mvs/iefjssib.h>
#include <mvs/subsys.h>

/* ============================================================
** The stub transport
** ============================================================ */

#define MAXSTEP 24

typedef struct step {
    int      r15;                       /* iefssreq return value            */
    int      retn;                      /* stored in SSOBRETN               */
    int      answer;                    /* 0: leave the UFSSSOB untouched   */
    int      rc;                        /* ufsssob.rc                       */
    unsigned data_len;                  /* ufsssob.data_len                 */
    char     data[UFSREQ_MAX_INLINE];   /* ufsssob.data                     */
    unsigned buf_n;                     /* bytes the router stores in buf_ptr */
    char     buf[LIBUFS_GETC_BUFSZ];
} STEP;

typedef struct seen {
    unsigned func;
    unsigned token;
    unsigned data_len;
    char     data[UFSREQ_MAX_INLINE];
    void    *buf_ptr;
    unsigned buf_len;
} SEEN;

static STEP     g_script[MAXSTEP];
static unsigned g_nstep;                /* scripted answers                 */
static SEEN     g_seen[MAXSTEP];
static unsigned g_ncall;                /* requests libufs issued           */
static unsigned g_overrun;              /* requests with no scripted answer */
static unsigned g_badssob;              /* malformed SSOB/SSIB/UFSSSOB      */

int
iefssreq(SSOB *ssob)
{
    UFSSSOB *u;
    const SSIB *s;
    const STEP *st;
    SEEN    *sn;
    unsigned n;

    if (!ssob || !ssob->SSOBINDV || !ssob->SSOBSSIB) {
        g_badssob++;
        return SSRTLERR;
    }
    u = (UFSSSOB *)ssob->SSOBINDV;
    s = (const SSIB *)ssob->SSOBSSIB;

    if (memcmp(ssob->SSOBID, SSOBID_EYE, 4) != 0
        || ssob->SSOBLEN != (unsigned short)sizeof(SSOB)
        || ssob->SSOBFUNC != (unsigned short)UFSD_SSOBFUNC
        || ssob->SSOBRETN != 0
        || memcmp(s->SSIBID, "SSIB", 4) != 0
        || s->SSIBLEN != (unsigned short)sizeof(SSIB)
        || memcmp(s->SSIBSSNM, UFSD_SSNAME, 4) != 0
        || memcmp(u->eye, UFSSSOB_EYE, 4) != 0)
        g_badssob++;

    n = g_ncall++;
    if (n < MAXSTEP) {
        sn = &g_seen[n];
        sn->func     = u->func;
        sn->token    = u->token;
        sn->data_len = u->data_len;
        memcpy(sn->data, u->data, sizeof(sn->data));
        sn->buf_ptr  = u->buf_ptr;
        sn->buf_len  = u->buf_len;
    }
    if (n >= g_nstep || n >= MAXSTEP) {
        g_overrun++;
        return SSRTDIST;
    }

    st = &g_script[n];
    ssob->SSOBRETN = st->retn;
    if (st->r15 != SSRTOK) return st->r15;

    if (st->answer) {
        u->rc        = st->rc;
        u->errno_val = 0;
        u->data_len  = st->data_len;
        memcpy(u->data, st->data, st->data_len);
    }
    if (st->buf_n > 0 && u->buf_ptr != NULL)
        memcpy(u->buf_ptr, st->buf,
               st->buf_n < u->buf_len ? st->buf_n : u->buf_len);
    return SSRTOK;
}

/* ============================================================
** Script helpers
** ============================================================ */

static void
begin(void)
{
    memset(g_script, 0, sizeof(g_script));
    memset(g_seen, 0, sizeof(g_seen));
    g_nstep   = 0;
    g_ncall   = 0;
    g_overrun = 0;
}

static STEP *
next_step(void)
{
    STEP *s;

    if (g_nstep >= MAXSTEP) {
        printf("  harness: script longer than MAXSTEP\n");
        g_nstep = MAXSTEP - 1;
    }
    s = &g_script[g_nstep++];
    memset(s, 0, sizeof(*s));
    return s;
}

static void
put_u32(char *p, unsigned v)
{
    memcpy(p, &v, sizeof(v));
}

static unsigned
get_u32(const char *p)
{
    unsigned v;
    memcpy(&v, p, sizeof(v));
    return v;
}

/* iefssreq itself fails: R15 != 0 */
static void
s_transport(int r15)
{
    STEP *s = next_step();
    s->r15 = r15;
}

/* The router returns normally but reports SSOBRETN != 0 and never fills in
** the response -- the UFSSSOB stays as the client left it. */
static void
s_refused(int retn)
{
    STEP *s = next_step();
    s->retn = retn;
}

/* The request is accepted and the response area is never written. */
static void
s_silent(void)
{
    (void)next_step();
}

/* The server answers rc != 0 with an empty response. */
static void
s_rc(int rc)
{
    STEP *s = next_step();
    s->answer = 1;
    s->rc     = rc;
    s->retn   = rc;
}

/* rc 0 with data_len bytes of response, all 0xEE. */
static STEP *
s_resp(unsigned len)
{
    STEP *s = next_step();
    s->answer   = 1;
    s->data_len = len;
    memset(s->data, 0xEE, len);
    return s;
}

/* rc 0, a single unsigned in data[0..3]. */
static void
s_ok_u32(unsigned v)
{
    STEP *s = s_resp(4U);
    put_u32(s->data, v);
}

/* rc 0, the FREAD answer: count in data[0..3], bytes after it. */
static void
s_read_inline(const char *bytes, unsigned n)
{
    STEP *s = s_resp(4U + n);
    put_u32(s->data, n);
    memcpy(s->data + 4, bytes, n);
}

/* rc 0, the FREAD answer on the 4K path: count in data[0..3], the bytes
** stored through buf_ptr as the router does. */
static void
s_read_buf(const char *bytes, unsigned n)
{
    STEP *s = s_resp(4U);
    put_u32(s->data, n);
    s->buf_n = n;
    memcpy(s->buf, bytes, n);
}

/* One DIRREAD / STAT response (UFSD_DIRREAD_RLEN bytes). */
static void
s_dirent(unsigned ino, unsigned size, unsigned short mode, const char *name)
{
    STEP *s = s_resp(UFSD_DIRREAD_RLEN);
    unsigned short nlink = 2;

    memset(s->data, 0, UFSD_DIRREAD_RLEN);
    put_u32(s->data, ino);
    put_u32(s->data + 4, size);
    memcpy(s->data + 8, &mode, sizeof(mode));
    memcpy(s->data + 10, &nlink, sizeof(nlink));
    strcpy(s->data + 12, name);
    strcpy(s->data + 80, "IBMUSER");
    strcpy(s->data + 89, "SYS1");
}

#define EXPECT_CALLS(n, msg)                                              \
    do {                                                                  \
        CHECK_EQ(g_ncall, (n), msg ": requests issued");                  \
        CHECK_EQ(g_overrun, 0, msg ": no request without a scripted answer"); \
    } while (0)

/* ============================================================
** Handles to run cases against
** ============================================================ */

#define TOKEN 0x00C0FFEEU

static UFS *
new_session(void)
{
    UFS *ufs;

    begin();
    s_ok_u32(TOKEN);
    ufs = ufsnew();
    CHECK(ufs != NULL && ufs->token == TOKEN, "fixture: session opens");
    begin();
    return ufs;
}

static UFSFILE *
open_file(UFS *ufs, const char *mode)
{
    UFSFILE *fp;

    begin();
    s_ok_u32(7U);
    fp = ufs_fopen(ufs, "/f", mode);
    CHECK(fp != NULL && fp->fd == 7, "fixture: file opens");
    begin();
    return fp;
}

static void
close_session(UFS **ufs)
{
    begin();
    s_ok_u32(0U);
    ufsfree(ufs);
    begin();
}

static void
close_file(UFSFILE **fp)
{
    begin();
    s_ok_u32(0U);
    ufs_fclose(fp);
    begin();
}

/* ============================================================
** The stub itself: each condition, asked for directly
** ============================================================ */

#define SENTINEL_RC   0x7777
#define SENTINEL_LEN  0x55U

static int
raw_call(UFSSSOB *u, SSOB *ssob, SSIB *ssib)
{
    static const char ssib_id[4] = { 'S', 'S', 'I', 'B' };

    memset(ssib, 0, sizeof(*ssib));
    memcpy(ssib->SSIBID, ssib_id, 4);
    ssib->SSIBLEN = (unsigned short)sizeof(*ssib);
    memcpy(ssib->SSIBSSNM, UFSD_SSNAME, 4);

    memset(ssob, 0, sizeof(*ssob));
    memcpy(ssob->SSOBID, SSOBID_EYE, 4);
    ssob->SSOBLEN  = (unsigned short)sizeof(*ssob);
    ssob->SSOBFUNC = (unsigned short)UFSD_SSOBFUNC;
    ssob->SSOBSSIB = ssib;
    ssob->SSOBINDV = u;

    memset(u, 0, sizeof(*u));
    memcpy(u->eye, UFSSSOB_EYE, 4);
    u->func     = UFSREQ_PING;
    u->rc       = SENTINEL_RC;
    u->data_len = SENTINEL_LEN;
    memset(u->data, 0x5A, sizeof(u->data));

    return iefssreq(ssob);
}

static void
test_stub_conditions(void)
{
    UFSSSOB u;
    SSOB    ssob;
    SSIB    ssib;
    int     r15;

    printf("\n--- stub: the four conditions ---\n");

    begin();
    s_transport(SSRTNOSS);
    r15 = raw_call(&u, &ssob, &ssib);
    CHECK_EQ(r15, SSRTNOSS, "transport failure: R15 is the scripted one");
    CHECK(u.rc == SENTINEL_RC && u.data_len == SENTINEL_LEN
          && (unsigned char)u.data[0] == 0x5A,
          "transport failure: UFSSSOB untouched");

    begin();
    s_refused(UFSD_RC_CORRUPT);
    r15 = raw_call(&u, &ssob, &ssib);
    CHECK_EQ(r15, SSRTOK, "router refusal: R15 is 0");
    CHECK_EQ(ssob.SSOBRETN, UFSD_RC_CORRUPT, "router refusal: SSOBRETN set");
    CHECK(u.rc == SENTINEL_RC && u.data_len == SENTINEL_LEN,
          "router refusal: UFSSSOB untouched");

    begin();
    s_rc(UFSD_RC_NOFILE);
    r15 = raw_call(&u, &ssob, &ssib);
    CHECK_EQ(r15, SSRTOK, "server error: R15 is 0");
    CHECK_EQ(u.rc, UFSD_RC_NOFILE, "server error: ufsssob.rc set");
    CHECK_EQ(u.data_len, 0, "server error: empty response");

    begin();
    s_resp(2U);
    r15 = raw_call(&u, &ssob, &ssib);
    CHECK(r15 == SSRTOK && u.rc == 0, "short response: rc 0");
    CHECK_EQ(u.data_len, 2, "short response: data_len is what was scripted");

    begin();
    s_silent();
    r15 = raw_call(&u, &ssob, &ssib);
    CHECK(r15 == SSRTOK && ssob.SSOBRETN == 0, "no answer: R15 and SSOBRETN 0");
    CHECK(u.rc == SENTINEL_RC && u.data_len == SENTINEL_LEN,
          "no answer: response area never written");

    begin();
    r15 = raw_call(&u, &ssob, &ssib);
    CHECK_EQ(r15, SSRTDIST, "unscripted request: R15 is a hard failure");
    CHECK_EQ(g_overrun, 1, "unscripted request: counted");
    begin();
}

/* ============================================================
** Requests as libufs puts them on the wire
** ============================================================ */

static void
test_wire(void)
{
    UFS     *ufs;
    UFSFILE *fp;
    char     buf[512];
    char     src[512];

    printf("\n--- wire: what libufs sends ---\n");

    begin();
    s_ok_u32(TOKEN);
    ufs = ufsnew();
    EXPECT_CALLS(1, "ufsnew");
    CHECK(ufs != NULL, "ufsnew: session returned");
    CHECK_EQ(g_seen[0].func, UFSREQ_SESS_OPEN, "ufsnew: SESS_OPEN");
    CHECK_EQ(g_seen[0].token, 0, "ufsnew: no token yet");
    CHECK_EQ(g_seen[0].data_len, 0, "ufsnew: no parameters");
    if (!ufs) return;
    CHECK_EQ(ufs->token, TOKEN, "ufsnew: token from the response");
    CHECK(memcmp(ufs->eye, "LIBUFSUFS", 8) == 0, "ufsnew: eye catcher");
    CHECK_EQ(ufs->last_rc, UFSD_RC_OK, "ufsnew: last_rc clear");
    CHECK(strcmp(ufs->cwd.path, "/") == 0, "ufsnew: cwd is /");

    begin();
    s_rc(UFSD_RC_OK);
    CHECK_EQ(ufs_mkdir(ufs, "/work/x"), UFSD_RC_OK, "mkdir: ok");
    EXPECT_CALLS(1, "mkdir");
    CHECK_EQ(g_seen[0].func, UFSREQ_MKDIR, "mkdir: MKDIR");
    CHECK_EQ(g_seen[0].token, TOKEN, "mkdir: session token");
    CHECK_EQ(g_seen[0].data_len, sizeof("/work/x"), "mkdir: path length + NUL");
    CHECK(strcmp(g_seen[0].data, "/work/x") == 0, "mkdir: path in data[]");

    begin();
    s_ok_u32(7U);
    fp = ufs_fopen(ufs, "/f", "rb");
    EXPECT_CALLS(1, "fopen r");
    CHECK_EQ(g_seen[0].func, UFSREQ_FOPEN, "fopen: FOPEN");
    CHECK_EQ(get_u32(g_seen[0].data), UFSD_OPEN_READ, "fopen rb: read mode");
    CHECK(strcmp(g_seen[0].data + 4, "/f") == 0, "fopen: path after the mode");
    CHECK_EQ(g_seen[0].data_len, 4 + 3, "fopen: mode + path + NUL");
    close_file(&fp);

    begin();
    s_ok_u32(7U);
    fp = ufs_fopen(ufs, "/f", "w");
    CHECK_EQ(get_u32(g_seen[0].data), UFSD_OPEN_WRITE, "fopen w: write mode");
    begin();
    if (!fp) return;

    memset(src, 'x', sizeof(src));
    begin();
    s_ok_u32(10U);
    CHECK_EQ(ufs_fwrite(src, 1, 10, fp), 10, "fwrite 10: all written");
    EXPECT_CALLS(1, "fwrite 10");
    CHECK_EQ(g_seen[0].func, UFSREQ_FWRITE, "fwrite: FWRITE");
    CHECK_EQ(get_u32(g_seen[0].data), 7, "fwrite: fd");
    CHECK_EQ(get_u32(g_seen[0].data + 4), 10, "fwrite: count");
    CHECK_EQ(g_seen[0].data_len, 8 + 10, "fwrite inline: header + bytes");
    CHECK(g_seen[0].buf_ptr == NULL, "fwrite inline: no buffer pointer");
    CHECK(memcmp(g_seen[0].data + 8, src, 10) == 0, "fwrite inline: bytes");

    begin();
    s_ok_u32(300U);
    CHECK_EQ(ufs_fwrite(src, 1, 300, fp), 300, "fwrite 300: all written");
    EXPECT_CALLS(1, "fwrite 300");
    CHECK_EQ(g_seen[0].data_len, 8, "fwrite 4K path: header only");
    CHECK(g_seen[0].buf_ptr == (void *)src, "fwrite 4K path: caller's buffer");
    CHECK_EQ(g_seen[0].buf_len, 300, "fwrite 4K path: buffer length");
    close_file(&fp);

    fp = open_file(ufs, "r");
    if (!fp) return;
    begin();
    s_read_inline("0123456789", 10U);
    CHECK_EQ(ufs_fread(buf, 1, 10, fp), 10, "fread 10: ten bytes");
    EXPECT_CALLS(1, "fread 10");
    CHECK_EQ(g_seen[0].func, UFSREQ_FREAD, "fread: FREAD");
    CHECK_EQ(get_u32(g_seen[0].data), 7, "fread: fd");
    CHECK_EQ(get_u32(g_seen[0].data + 4), 10, "fread: count");
    CHECK(g_seen[0].buf_ptr == NULL, "fread inline: no buffer pointer");
    CHECK(memcmp(buf, "0123456789", 10) == 0, "fread inline: bytes copied");

    begin();
    memset(src, 'q', sizeof(src));
    s_read_buf(src, 300U);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(ufs_fread(buf, 1, 300, fp), 300, "fread 300: all bytes");
    EXPECT_CALLS(1, "fread 300");
    CHECK(g_seen[0].buf_ptr == (void *)buf, "fread 4K path: caller's buffer");
    CHECK_EQ(g_seen[0].buf_len, 300, "fread 4K path: buffer length");
    CHECK(memcmp(buf, src, 300) == 0, "fread 4K path: bytes stored by the router");
    close_file(&fp);

    begin();
    s_ok_u32(0U);
    ufsfree(&ufs);
    EXPECT_CALLS(1, "ufsfree");
    CHECK_EQ(g_seen[0].func, UFSREQ_SESS_CLOSE, "ufsfree: SESS_CLOSE");
    CHECK_EQ(g_seen[0].token, TOKEN, "ufsfree: session token");
    CHECK(ufs == NULL, "ufsfree: handle cleared");
}

/* ============================================================
** Transport failure: iefssreq returns R15 != 0
** ============================================================ */

static void
test_transport_failure(void)
{
    UFS       *ufs;
    UFSFILE   *fp;
    UFSDLIST   st;
    char       buf[64];
    static const int r15s[] = { SSRTNSUP, SSRTNTUP, SSRTNOSS, SSRTDIST, SSRTLERR };

    printf("\n--- transport failure (R15 != 0) ---\n");

    for (int i = 0; i < 5; i++) {
        begin();
        s_transport(r15s[i]);
        CHECK(ufsnew() == NULL, "ufsnew: NULL for every non-zero R15");
        EXPECT_CALLS(1, "ufsnew");
    }

    ufs = new_session();
    if (!ufs) return;

    begin();
    s_transport(SSRTNOSS);
    CHECK_EQ(ufs_mkdir(ufs, "/a"), -1, "mkdir: -1");
    CHECK_EQ(ufs_last_rc(ufs), -1, "mkdir: last_rc -1");
    EXPECT_CALLS(1, "mkdir");

    begin();
    s_transport(SSRTNOSS);
    CHECK_EQ(ufs_rmdir(ufs, "/a"), -1, "rmdir: -1");
    begin();
    s_transport(SSRTNOSS);
    CHECK_EQ(ufs_remove(ufs, "/a"), -1, "remove: -1");

    begin();
    s_transport(SSRTNOSS);
    CHECK_EQ(ufs_chgdir(ufs, "/a"), -1, "chgdir: -1");
    CHECK(strcmp(ufs->cwd.path, "/") == 0, "chgdir: cwd not moved");

    begin();
    s_transport(SSRTNOSS);
    CHECK_EQ(ufs_setuser(ufs, "IBMUSER", "SYS1"), -1, "setuser: -1");

    begin();
    s_transport(SSRTNOSS);
    CHECK(ufs_fopen(ufs, "/f", "r") == NULL, "fopen: NULL");
    CHECK_EQ(ufs_last_rc(ufs), -1, "fopen: last_rc -1");

    begin();
    s_transport(SSRTNOSS);
    CHECK_EQ(ufs_stat(ufs, "/f", &st), -1, "stat: -1");
    CHECK_EQ(ufs_last_rc(ufs), -1, "stat: last_rc -1");

    begin();
    s_transport(SSRTNOSS);
    CHECK(ufs_diropen(ufs, "/", "*") == NULL, "diropen: NULL");
    EXPECT_CALLS(1, "diropen");

    fp = open_file(ufs, "r");
    if (fp) {
        begin();
        s_transport(SSRTNOSS);
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "fread: nothing read");
        EXPECT_CALLS(1, "fread");
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "fread: a failed handle reads nothing");
        CHECK_EQ(g_ncall, 1, "fread: the failed handle sends no further request");
        close_file(&fp);
    }

    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_transport(SSRTNOSS);
        CHECK_EQ(ufs_fwrite("abc", 1, 3, fp), 0, "fwrite: nothing written");
        EXPECT_CALLS(1, "fwrite");
        CHECK_EQ(ufs_fwrite("abc", 1, 3, fp), 0, "fwrite: a failed handle writes nothing");
        CHECK_EQ(g_ncall, 1, "fwrite: the failed handle sends no further request");
        CHECK_EQ(ufs_fputc('x', fp), UFS_EOF, "fputc: refused on a failed handle");
        close_file(&fp);
    }

    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_transport(SSRTNOSS);
        CHECK_EQ(ufs_fputs("abc", fp), 3, "fputs: buffered, no request yet");
        CHECK_EQ(g_ncall, 0, "fputs: nothing sent before the flush");
        ufs_fsync(fp);
        EXPECT_CALLS(1, "fsync");
        CHECK_EQ(g_seen[0].func, UFSREQ_FWRITE, "fsync: flushes through FWRITE");
        CHECK_EQ(ufs_fputc('x', fp), UFS_EOF, "fputc after a failed flush: refused");
        close_file(&fp);
    }

    {
        UFSFILE *gone = open_file(ufs, "r");
        begin();
        s_transport(SSRTNOSS);
        ufs_fclose(&gone);
        CHECK(gone == NULL, "fclose: handle freed although FCLOSE failed");
    }

    begin();
    s_transport(SSRTNOSS);
    ufsfree(&ufs);
    CHECK(ufs == NULL, "ufsfree: handle freed although SESS_CLOSE failed");
}

/* ============================================================
** Router refusal: R15 = 0, SSOBRETN != 0, response never written
** ============================================================ */

static void
test_router_refusal(void)
{
    UFS      *ufs;
    UFSDLIST  st;

    printf("\n--- router refusal (SSOBRETN != 0) ---\n");

    begin();
    s_refused(UFSD_RC_CORRUPT);
    CHECK(ufsnew() == NULL, "ufsnew: NULL");
    EXPECT_CALLS(1, "ufsnew");

    ufs = new_session();
    if (!ufs) return;

    begin();
    s_refused(UFSD_RC_NOREQ);
    CHECK(ufs_diropen(ufs, "/", "*") == NULL, "diropen: NULL");
    EXPECT_CALLS(1, "diropen");

    begin();
    s_refused(UFSD_RC_CORRUPT);
    CHECK(ufs_stat(ufs, "/f", &st) != UFSD_RC_OK, "stat: not a success");

    close_session(&ufs);
}

/* ============================================================
** Server error: R15 = 0, ufsssob.rc != 0
** ============================================================ */

static void
test_server_error(void)
{
    UFS      *ufs;
    UFSFILE  *fp;
    UFSDLIST  st;
    char      buf[512];
    STEP     *s;

    printf("\n--- server error (rc != 0) ---\n");

    begin();
    s_rc(UFSD_RC_NOSPACE);
    CHECK(ufsnew() == NULL, "ufsnew: NULL");

    ufs = new_session();
    if (!ufs) return;

    begin();
    s_rc(UFSD_RC_EXIST);
    CHECK_EQ(ufs_mkdir(ufs, "/a"), UFSD_RC_EXIST, "mkdir: rc passed through");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_EXIST, "mkdir: last_rc");

    begin();
    s_rc(UFSD_RC_NOTEMPTY);
    CHECK_EQ(ufs_rmdir(ufs, "/a"), UFSD_RC_NOTEMPTY, "rmdir: rc passed through");

    begin();
    s_rc(UFSD_RC_NOFILE);
    CHECK_EQ(ufs_remove(ufs, "/a"), UFSD_RC_NOFILE, "remove: rc passed through");

    begin();
    s_rc(UFSD_RC_NOFILE);
    CHECK_EQ(ufs_chgdir(ufs, "/nowhere"), UFSD_RC_NOFILE, "chgdir: rc passed through");
    CHECK(strcmp(ufs->cwd.path, "/") == 0, "chgdir: cwd not moved");

    begin();
    s_rc(UFSD_RC_OK);
    CHECK_EQ(ufs_chgdir(ufs, "/work"), UFSD_RC_OK, "chgdir: ok");
    CHECK(strcmp(ufs->cwd.path, "/work") == 0, "chgdir: cwd mirrored on success");

    begin();
    s_rc(UFSD_RC_EACCES);
    CHECK_EQ(ufs_setuser(ufs, "IBMUSER", NULL), UFSD_RC_EACCES, "setuser: rc passed through");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_EACCES, "setuser: last_rc");

    begin();
    s_rc(UFSD_RC_NOFILE);
    CHECK(ufs_fopen(ufs, "/f", "r") == NULL, "fopen: NULL");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_NOFILE, "fopen: last_rc");

    begin();
    s_rc(UFSD_RC_NOFILE);
    CHECK_EQ(ufs_stat(ufs, "/f", &st), UFSD_RC_NOFILE, "stat: rc passed through");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_NOFILE, "stat: last_rc");

    begin();
    s_rc(UFSD_RC_NOTDIR);
    CHECK(ufs_diropen(ufs, "/f", "*") == NULL, "diropen: NULL");

    fp = open_file(ufs, "r");
    if (fp) {
        begin();
        s_rc(UFSD_RC_IO);
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "fread: nothing read");
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_IO, "fread: ferror is the server's rc");
        EXPECT_CALLS(1, "fread");
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "fread: failed handle reads nothing");
        CHECK_EQ(g_ncall, 1, "fread: no request after the error");
        ufs_clearerr(fp);
        CHECK_EQ(ufs_ferror(fp), 0, "clearerr: error cleared");
        close_file(&fp);
    }

    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s = s_resp(4U);
        put_u32(s->data, 100U);
        s->rc = UFSD_RC_NOSPACE;
        memset(buf, 'z', sizeof(buf));
        CHECK_EQ(ufs_fwrite(buf, 1, 300, fp), 100,
                 "fwrite: bytes the server did write are reported");
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_NOSPACE, "fwrite: ferror is the server's rc");
        EXPECT_CALLS(1, "fwrite");
        close_file(&fp);
    }

    close_session(&ufs);
}

/* ============================================================
** Short response: rc 0, but less than the caller needs
** ============================================================ */

static void
test_short_response(void)
{
    UFS      *ufs;
    UFSDLIST  st;
    const UFSDDESC *dd;

    printf("\n--- short or missing response ---\n");

    begin();
    s_resp(2U);
    CHECK(ufsnew() == NULL, "ufsnew: 2 bytes is no token");
    begin();
    s_silent();
    CHECK(ufsnew() == NULL, "ufsnew: unwritten response is no token");

    ufs = new_session();
    if (!ufs) return;

    begin();
    s_resp(3U);
    CHECK(ufs_fopen(ufs, "/f", "r") == NULL, "fopen: 3 bytes is no fd");

    begin();
    s_resp(UFSD_DIRREAD_RLEN - 1U);
    CHECK_EQ(ufs_stat(ufs, "/f", &st), UFSD_RC_INVALID, "stat: one byte short");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_INVALID, "stat: last_rc INVALID");

    begin();
    s_silent();
    CHECK_EQ(ufs_stat(ufs, "/f", &st), UFSD_RC_INVALID, "stat: unwritten response");

    begin();
    s_resp(3U);
    CHECK(ufs_diropen(ufs, "/", "*") == NULL, "diropen: 3 bytes is no fd");
    EXPECT_CALLS(1, "diropen short");

    /* DIROPEN fine, the first DIRREAD comes back too short to hold an inode:
    ** libufs gives the server's directory handle back and reports I/O. */
    begin();
    s_ok_u32(5U);
    s_resp(2U);
    s_ok_u32(0U);
    dd = ufs_diropen(ufs, "/", "*");
    CHECK(dd == NULL, "diropen: short DIRREAD fails the open");
    EXPECT_CALLS(3, "diropen short DIRREAD");
    CHECK_EQ(g_seen[2].func, UFSREQ_DIRCLOSE, "diropen: DIRCLOSE sent on the way out");
    CHECK_EQ(get_u32(g_seen[2].data), 5, "diropen: DIRCLOSE names the directory fd");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_IO, "diropen: last_rc IO");

    begin();
    s_ok_u32(5U);
    s_transport(SSRTNOSS);
    s_ok_u32(0U);
    dd = ufs_diropen(ufs, "/", "*");
    CHECK(dd == NULL, "diropen: transport failure on DIRREAD fails the open");
    CHECK_EQ(g_seen[2].func, UFSREQ_DIRCLOSE, "diropen: DIRCLOSE sent on the way out");

    begin();
    s_ok_u32(5U);
    s_rc(UFSD_RC_IO);
    s_ok_u32(0U);
    dd = ufs_diropen(ufs, "/", "*");
    CHECK(dd == NULL, "diropen: server error on DIRREAD fails the open");
    CHECK_EQ(g_seen[2].func, UFSREQ_DIRCLOSE, "diropen: DIRCLOSE sent on the way out");

    close_session(&ufs);
}

/* ============================================================
** Answers that are fine
** ============================================================ */

static void
test_success(void)
{
    UFS      *ufs;
    UFSFILE  *fp;
    UFSDLIST  st;
    const UFSDLIST *e;
    UFSDDESC *dd;
    char      buf[64];

    printf("\n--- success paths ---\n");

    ufs = new_session();
    if (!ufs) return;

    begin();
    s_dirent(9U, 1234U, 0x41EDU, "sub");
    CHECK_EQ(ufs_stat(ufs, "/sub", &st), UFSD_RC_OK, "stat: ok");
    CHECK_EQ(g_seen[0].func, UFSREQ_STAT, "stat: STAT");
    CHECK_EQ(st.inode_number, 9, "stat: inode");
    CHECK_EQ(st.filesize, 1234, "stat: size");
    CHECK(strcmp(st.attr, "drwxr-xr-x") == 0, "stat: mode decoded");
    CHECK_EQ(st.nlink, 2, "stat: nlink");
    CHECK(strcmp(st.name, "sub") == 0, "stat: name");
    CHECK(strcmp(st.owner, "IBMUSER") == 0, "stat: owner");
    CHECK(strcmp(st.group, "SYS1") == 0, "stat: group");

    /* Entries arrive unsorted and are handed out sorted, dot entries first. */
    begin();
    s_ok_u32(5U);
    s_dirent(11U, 10U, 0x81A4U, "b");
    s_dirent(12U, 20U, 0x81A4U, "a");
    s_dirent(2U, 0U, 0x41EDU, ".");
    s_ok_u32(0U);
    dd = ufs_diropen(ufs, "/", "*");
    EXPECT_CALLS(5, "diropen");
    CHECK(dd != NULL, "diropen: handle");
    if (dd) {
        CHECK_EQ(g_seen[1].func, UFSREQ_DIRREAD, "diropen: DIRREAD");
        CHECK_EQ(get_u32(g_seen[1].data), 5, "diropen: DIRREAD names the fd");
        e = ufs_dirread(dd);
        CHECK(e && strcmp(e->name, ".") == 0, "dirread: . first");
        e = ufs_dirread(dd);
        CHECK(e && strcmp(e->name, "a") == 0, "dirread: a");
        CHECK(e && strcmp(e->attr, "-rw-r--r--") == 0, "dirread: file mode decoded");
        e = ufs_dirread(dd);
        CHECK(e && strcmp(e->name, "b") == 0, "dirread: b");
        CHECK(ufs_dirread(dd) == NULL, "dirread: end of directory");
        begin();
        s_ok_u32(0U);
        ufs_dirclose(&dd);
        EXPECT_CALLS(1, "dirclose");
        CHECK_EQ(g_seen[0].func, UFSREQ_DIRCLOSE, "dirclose: DIRCLOSE");
        CHECK_EQ(get_u32(g_seen[0].data), 5, "dirclose: fd");
        CHECK(dd == NULL, "dirclose: handle cleared");
    }

    fp = open_file(ufs, "r");
    if (fp) {
        begin();
        s_read_inline("abcd", 4U);
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 4, "fread: short read returns what came");
        CHECK(ufs_feof(fp), "fread: short read sets EOF");
        CHECK_EQ(ufs_ferror(fp), 0, "fread: short read is no error");
        ufs_clearerr(fp);

        begin();
        s_read_inline("", 0U);
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "fread: zero bytes is EOF");
        CHECK(ufs_feof(fp), "fread: EOF flag");
        close_file(&fp);
    }

    /* One refill, one request: fgetc/fgets read ahead through the 4K path. */
    fp = open_file(ufs, "r");
    if (fp) {
        char line[16];

        begin();
        s_read_buf("hello\nwor", 9U);
        CHECK(ufs_fgets(line, sizeof(line), fp) != NULL
              && strcmp(line, "hello\n") == 0, "fgets: first line");
        CHECK(ufs_fgets(line, sizeof(line), fp) != NULL
              && strcmp(line, "wor") == 0, "fgets: tail without newline");
        CHECK(ufs_fgets(line, sizeof(line), fp) == NULL, "fgets: then end of file");
        EXPECT_CALLS(1, "fgets");
        CHECK_EQ(g_seen[0].buf_len, LIBUFS_GETC_BUFSZ, "fgets: refill is one 4K read");
        close_file(&fp);
    }

    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_ok_u32(3U);
        s_ok_u32(0U);
        CHECK_EQ(ufs_fputs("abc", fp), 3, "fputs: length");
        CHECK_EQ(g_ncall, 0, "fputs: buffered");
        ufs_fclose(&fp);
        EXPECT_CALLS(2, "fclose with pending bytes");
        CHECK_EQ(g_seen[0].func, UFSREQ_FWRITE, "fclose: flushes first");
        CHECK_EQ(g_seen[0].data_len, 8 + 3, "fclose: flush carries the 3 bytes");
        CHECK_EQ(g_seen[1].func, UFSREQ_FCLOSE, "fclose: then FCLOSE");
        CHECK_EQ(get_u32(g_seen[1].data), 7, "fclose: fd");
        CHECK(fp == NULL, "fclose: handle cleared");
    }

    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_ok_u32(4U);
        CHECK_EQ(ufs_fwrite("abcdefgh", 1, 8, fp), 4, "fwrite: short write returns what was written");
        CHECK_EQ(ufs_ferror(fp), 0, "fwrite: short write is no error");
        close_file(&fp);
    }

    close_session(&ufs);
}

/* ============================================================
** A failed request is never reported as a success (#77, #78, #79)
** ============================================================ */

static void
test_error_propagation(void)
{
    UFS      *ufs;
    UFSFILE  *fp;
    UFSDLIST  st;
    char      buf[512];

    printf("\n--- error propagation ---\n");

    ufs = new_session();
    if (!ufs) return;

    /* ferror() is non-zero on every handle libufs has flagged as failed. */
    fp = open_file(ufs, "r");
    if (fp) {
        begin();
        s_transport(SSRTNOSS);
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "transport/fread: nothing read");
        CHECK(ufs_ferror(fp) != 0, "transport/fread: ferror is non-zero");
        ufs_clearerr(fp);
        CHECK_EQ(ufs_ferror(fp), 0, "transport/fread: clearerr clears it");
        close_file(&fp);
    }

    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_transport(SSRTNOSS);
        CHECK_EQ(ufs_fwrite("abc", 1, 3, fp), 0, "transport/fwrite: nothing written");
        CHECK(ufs_ferror(fp) != 0, "transport/fwrite: ferror is non-zero");
        close_file(&fp);
    }

    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_transport(SSRTNOSS);
        CHECK_EQ(ufs_fputs("abc", fp), 3, "transport/fsync: buffered");
        ufs_fsync(fp);
        CHECK(ufs_ferror(fp) != 0, "transport/fsync: ferror is non-zero");
        close_file(&fp);
    }

    /* The router refused: R15 is 0, SSOBRETN is not, the response was never
    ** written.  The caller must see the router's code, not a success. */
    begin();
    s_refused(UFSD_RC_CORRUPT);
    CHECK_EQ(ufs_mkdir(ufs, "/a"), UFSD_RC_CORRUPT, "refused/mkdir: router's code");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_CORRUPT, "refused/mkdir: last_rc");

    begin();
    s_refused(UFSD_RC_NOREQ);
    CHECK_EQ(ufs_rmdir(ufs, "/a"), UFSD_RC_NOREQ, "refused/rmdir: router's code");

    begin();
    s_refused(UFSD_RC_NOREQ);
    CHECK_EQ(ufs_remove(ufs, "/a"), UFSD_RC_NOREQ, "refused/remove: router's code");

    begin();
    s_refused(UFSD_RC_CORRUPT);
    CHECK_EQ(ufs_chgdir(ufs, "/work"), UFSD_RC_CORRUPT, "refused/chgdir: router's code");
    CHECK(strcmp(ufs->cwd.path, "/") == 0, "refused/chgdir: cwd not moved");

    begin();
    s_refused(UFSD_RC_CORRUPT);
    CHECK_EQ(ufs_setuser(ufs, "IBMUSER", "SYS1"), UFSD_RC_CORRUPT,
             "refused/setuser: router's code");

    begin();
    s_refused(UFSD_RC_CORRUPT);
    CHECK(ufs_fopen(ufs, "/f", "r") == NULL, "refused/fopen: NULL");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_CORRUPT, "refused/fopen: last_rc");

    begin();
    s_refused(UFSD_RC_NOREQ);
    CHECK_EQ(ufs_stat(ufs, "/f", &st), UFSD_RC_NOREQ, "refused/stat: router's code");
    CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_NOREQ, "refused/stat: last_rc");

    fp = open_file(ufs, "r");
    if (fp) {
        begin();
        s_refused(UFSD_RC_CORRUPT);
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "refused/fread: nothing read");
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_CORRUPT, "refused/fread: ferror is the router's code");
        CHECK(!ufs_feof(fp), "refused/fread: no EOF");
        close_file(&fp);
    }

    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_refused(UFSD_RC_CORRUPT);
        CHECK_EQ(ufs_fwrite("abcdefghij", 1, 10, fp), 0, "refused/fwrite inline: nothing written");
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_CORRUPT, "refused/fwrite inline: ferror is the router's code");
        ufs_clearerr(fp);

        begin();
        s_refused(UFSD_RC_NOREQ);
        memset(buf, 'z', sizeof(buf));
        CHECK_EQ(ufs_fwrite(buf, 1, 300, fp), 0, "refused/fwrite 4K: nothing written");
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_NOREQ, "refused/fwrite 4K: ferror is the router's code");
        close_file(&fp);
    }

    /* The server rejected the write before counting anything: the response
    ** carries an rc and no length, and the response area still holds what
    ** the client put there -- the fd, not a byte count. */
    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_rc(UFSD_RC_BADFD);
        CHECK_EQ(ufs_fwrite("abcdefghij", 1, 10, fp), 0, "rejected/fwrite inline: no bytes counted");
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_BADFD, "rejected/fwrite inline: ferror is the server's rc");
        ufs_clearerr(fp);

        begin();
        s_rc(UFSD_RC_INVALID);
        s_rc(UFSD_RC_INVALID);      /* INVALID is retried once, inline (#80) */
        memset(buf, 'z', sizeof(buf));
        CHECK_EQ(ufs_fwrite(buf, 1, 300, fp), 0, "rejected/fwrite 4K: no bytes counted");
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_INVALID, "rejected/fwrite 4K: ferror is the server's rc");
        ufs_clearerr(fp);

        /* rc 0 but no byte count: the response is not trusted either. */
        begin();
        (void)s_resp(0U);
        CHECK_EQ(ufs_fwrite("abcdefghij", 1, 10, fp), 0, "no count/fwrite: nothing counted");
        CHECK(ufs_ferror(fp) != 0, "no count/fwrite: flagged as an error");
        close_file(&fp);
    }

    fp = open_file(ufs, "r");
    if (fp) {
        begin();
        (void)s_resp(0U);
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "no count/fread: nothing read");
        CHECK(ufs_ferror(fp) != 0, "no count/fread: flagged as an error");
        close_file(&fp);
    }

    close_session(&ufs);
}

/* ============================================================
** 4K write while the router's CSA buffer pool is empty (#80)
**
** The router has no buffer to copy the client's data into, so the request
** reaches do_fwrite with data_len 8 and no data: it answers
** UFSD_RC_INVALID before it writes anything and answers a byte count of 0
** (data_len 4).  That is back-pressure, not a bad request -- the same chunk
** must go again through the inline path.
** ============================================================ */

/* The empty-pool answer as do_fwrite gives it: rc INVALID, byte count 0. */
static void
s_pool_empty(void)
{
    STEP *s = s_resp(4U);

    put_u32(s->data, 0U);
    s->rc   = UFSD_RC_INVALID;
    s->retn = UFSD_RC_INVALID;
}

/* The same rejection with no response data at all. */
static void
s_pool_empty_nolen(void)
{
    s_rc(UFSD_RC_INVALID);
}

static void
test_write_pool_empty(void)
{
    UFS      *ufs;
    UFSFILE  *fp;
    char      src[4096 + 300];
    unsigned  i;

    for (i = 0; i < sizeof(src); i++)
        src[i] = (char)('A' + (i % 26));

    ufs = new_session();
    if (!ufs) return;

    /* 300 bytes: 4K path refused, the same bytes go inline in 248 + 52 */
    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_pool_empty();
        s_ok_u32(248U);
        s_ok_u32(52U);
        CHECK_EQ(ufs_fwrite(src, 1, 300, fp), 300, "pool empty: all 300 bytes written");
        CHECK_EQ(ufs_ferror(fp), 0, "pool empty: no error on the handle");
        EXPECT_CALLS(3, "pool empty, 300 bytes");
        CHECK(g_seen[0].buf_ptr == (void *)src, "pool empty: first try is the 4K path");
        CHECK_EQ(g_seen[1].func, UFSREQ_FWRITE, "pool empty: retry is FWRITE");
        CHECK(g_seen[1].buf_ptr == NULL, "pool empty: retry carries no buffer pointer");
        CHECK_EQ(get_u32(g_seen[1].data + 4), 248, "pool empty: retry count is the inline maximum");
        CHECK_EQ(g_seen[1].data_len, 8 + 248, "pool empty: retry carries header + data");
        CHECK(memcmp(g_seen[1].data + 8, src, 248) == 0, "pool empty: retry bytes, first piece");
        CHECK(g_seen[2].buf_ptr == NULL, "pool empty: second piece is inline too");
        CHECK_EQ(get_u32(g_seen[2].data + 4), 52, "pool empty: second piece count");
        CHECK(memcmp(g_seen[2].data + 8, src + 248, 52) == 0, "pool empty: retry bytes, second piece");
        close_file(&fp);
    }

    /* the same refusal without a returned length is recognised too */
    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_pool_empty_nolen();
        s_ok_u32(248U);
        s_ok_u32(52U);
        CHECK_EQ(ufs_fwrite(src, 1, 300, fp), 300, "pool empty, no length: all 300 bytes written");
        CHECK_EQ(ufs_ferror(fp), 0, "pool empty, no length: no error on the handle");
        EXPECT_CALLS(3, "pool empty, no length");
        close_file(&fp);
    }

    /* 4396 bytes: the pool is only skipped for the chunk it refused */
    fp = open_file(ufs, "w");
    if (fp) {
        unsigned n;

        begin();
        s_pool_empty();
        for (n = 0; n < 16; n++) s_ok_u32(248U);
        s_ok_u32(128U);             /* 16 * 248 + 128 = 4096 */
        s_ok_u32(300U);             /* the rest goes the 4K way again */
        CHECK_EQ(ufs_fwrite(src, 1, sizeof(src), fp), sizeof(src),
                 "pool empty: 4396 bytes all written");
        CHECK_EQ(ufs_ferror(fp), 0, "pool empty, 4396: no error on the handle");
        EXPECT_CALLS(1 + 17 + 1, "pool empty, 4396 bytes");
        CHECK(g_seen[0].buf_ptr == (void *)src, "pool empty, 4396: first try is the 4K path");
        CHECK_EQ(g_seen[0].buf_len, 4096, "pool empty, 4396: first try is a full chunk");
        CHECK(g_seen[1].buf_ptr == NULL, "pool empty, 4396: refused chunk goes inline");
        CHECK_EQ(g_seen[17].data_len, 8 + 128, "pool empty, 4396: last inline piece");
        CHECK(memcmp(g_seen[17].data + 8, src + 16 * 248, 128) == 0,
              "pool empty, 4396: last inline piece bytes");
        CHECK(g_seen[18].buf_ptr == (void *)(src + 4096),
              "pool empty, 4396: next chunk tries the 4K path again");
        close_file(&fp);
    }

    /* a write that is invalid for real fails on the retry too: reported */
    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_pool_empty();
        s_rc(UFSD_RC_INVALID);
        (void)ufs_fwrite(src, 1, 300, fp);
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_INVALID, "invalid write: ferror is INVALID");
        EXPECT_CALLS(2, "invalid write retried once");
        close_file(&fp);
    }

    /* inline-sized write answered INVALID: there is nothing to retry */
    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_rc(UFSD_RC_INVALID);
        (void)ufs_fwrite(src, 1, 100, fp);
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_INVALID, "inline invalid: ferror is INVALID");
        EXPECT_CALLS(1, "inline invalid, no retry");
        close_file(&fp);
    }

    /* INVALID after part of the data was written is no refusal: no retry */
    fp = open_file(ufs, "w");
    if (fp) {
        STEP *st;

        begin();
        st = s_resp(4U);
        put_u32(st->data, 100U);
        st->rc   = UFSD_RC_INVALID;
        st->retn = UFSD_RC_INVALID;
        CHECK_EQ(ufs_fwrite(src, 1, 300, fp), 100, "partial then INVALID: the 100 bytes counted");
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_INVALID, "partial then INVALID: ferror is INVALID");
        EXPECT_CALLS(1, "partial then INVALID, no retry");
        close_file(&fp);
    }

    /* any other error on the 4K path is not back-pressure: no retry */
    fp = open_file(ufs, "w");
    if (fp) {
        begin();
        s_rc(UFSD_RC_IO);
        (void)ufs_fwrite(src, 1, 300, fp);
        CHECK_EQ(ufs_ferror(fp), UFSD_RC_IO, "4K path IO error: ferror is IO");
        EXPECT_CALLS(1, "4K path IO error, no retry");
        close_file(&fp);
    }

    close_session(&ufs);
}

/* ============================================================
** Path length: 255 characters fit the inline area with their NUL,
** 256 or more are refused before any request is built
** ============================================================ */

typedef int (*PATHFN)(UFS *ufs, const char *path);

static void
check_path_limit(const char *name, PATHFN fn, unsigned func, UFS *ufs)
{
    char     path[UFSREQ_MAX_INLINE + 8];
    char     msg[48];

    /* 255 characters: sent whole, NUL-terminated */
    memset(path, 'a', sizeof(path));
    path[0] = '/';
    path[255] = '\0';
    begin();
    s_rc(UFSD_RC_OK);
    snprintf(msg, sizeof(msg), "%s 255", name);
    CHECK_EQ(fn(ufs, path), UFSD_RC_OK, msg);
    CHECK_EQ(g_ncall, 1, "255: one request issued");
    CHECK_EQ(g_overrun, 0, "255: no request without a scripted answer");
    CHECK_EQ(g_seen[0].func, func, "255: function");
    CHECK_EQ(g_seen[0].data_len, 256U, "255: data_len counts the NUL");
    CHECK(memcmp(g_seen[0].data, path, 256U) == 0,
          "255: all 255 characters and the NUL arrive");
    CHECK_EQ(g_seen[0].data[255], '\0', "255: last byte is NUL");

    /* 256, 257 and a long one: INVALID, nothing sent, last_rc set */
    for (unsigned n = 256U; n <= 262U; n += (n == 257U) ? 5U : 1U) {
        memset(path, 'b', sizeof(path));
        path[0] = '/';
        path[n] = '\0';
        begin();
        ufs->last_rc = 0;
        snprintf(msg, sizeof(msg), "%s %u", name, n);
        CHECK_EQ(fn(ufs, path), UFSD_RC_INVALID, msg);
        CHECK_EQ(g_ncall, 0, "no request issued");
        CHECK_EQ(ufs_last_rc(ufs), UFSD_RC_INVALID, "last_rc INVALID");
    }
}

static void
test_path_limit(void)
{
    UFS *ufs;

    printf("\n--- path length limit (chgdir, mkdir, rmdir, remove) ---\n");

    ufs = new_session();
    if (!ufs) return;

    check_path_limit("chgdir", ufs_chgdir, UFSREQ_CHGDIR, ufs);
    check_path_limit("mkdir", ufs_mkdir, UFSREQ_MKDIR, ufs);
    check_path_limit("rmdir", ufs_rmdir, UFSREQ_RMDIR, ufs);
    check_path_limit("remove", ufs_remove, UFSREQ_REMOVE, ufs);

    close_session(&ufs);
}

/* ============================================================
** FREAD when the server cannot deliver 4K data (#135)
**
** A 4K request (want > 252) is answered inline when the server has no CSA
** pool buffer to spare or its staging malloc fails: count clamped to 252,
** data in data[4..], data_len = 4 + count, buf_ptr left alone by the router.
** data_len > 4 is what tells ufs_fread, not buf_ptr.  The position on the
** server has advanced by exactly the bytes in the reply, so 252 is a full
** chunk and only a shorter reply is EOF.
** ============================================================ */

static void
test_read_pool_empty(void)
{
    UFS     *ufs;
    UFSFILE *fp;
    char     buf[800];
    char     src[800];

    printf("\n--- FREAD without 4K data (#135) ---\n");

    ufs = new_session();
    if (!ufs) return;

    /* inline answer to a 300 byte request, the rest asked for next */
    fp = open_file(ufs, "r");
    if (fp) {
        memset(src, 'a', 252);
        memset(src + 252, 'b', 48);
        begin();
        s_read_inline(src, 252U);
        s_read_inline(src + 252, 48U);
        memset(buf, 0x5A, sizeof(buf));
        CHECK_EQ(ufs_fread(buf, 1, 300, fp), 300, "inline answer: all 300 bytes");
        CHECK(memcmp(buf, src, 300) == 0, "inline answer: the bytes of the reply");
        CHECK(!ufs_feof(fp), "inline answer: not EOF");
        CHECK_EQ(ufs_ferror(fp), 0, "inline answer: no error");
        CHECK_EQ(g_ncall, 2, "inline answer: the rest is asked for");
        CHECK_EQ(get_u32(g_seen[0].data + 4), 300, "inline answer: first ask is 300");
        CHECK_EQ(get_u32(g_seen[1].data + 4), 48, "inline answer: then what is left");
        CHECK(g_seen[0].buf_ptr == (void *)buf, "inline answer: first ask offered the buffer");
        CHECK(g_seen[1].buf_ptr == NULL, "inline answer: the rest is an inline ask");
        close_file(&fp);
    }

    /* several full inline chunks in a row, then a short one */
    fp = open_file(ufs, "r");
    if (fp) {
        memset(src, 'c', sizeof(src));
        begin();
        s_read_inline(src, 252U);
        s_read_inline(src + 252, 252U);
        s_read_inline(src + 504, 196U);
        memset(buf, 0x5A, sizeof(buf));
        CHECK_EQ(ufs_fread(buf, 1, 700, fp), 700, "inline chunks: all 700 bytes");
        CHECK(memcmp(buf, src, 700) == 0, "inline chunks: the bytes of the replies");
        CHECK(!ufs_feof(fp), "inline chunks: not EOF");
        CHECK_EQ(g_ncall, 3, "inline chunks: three requests");
        close_file(&fp);
    }

    /* an inline answer shorter than 252 to a 4K request is EOF */
    fp = open_file(ufs, "r");
    if (fp) {
        memset(src, 'd', 100);
        begin();
        s_read_inline(src, 100U);
        memset(buf, 0x5A, sizeof(buf));
        CHECK_EQ(ufs_fread(buf, 1, 300, fp), 100, "short inline answer: 100 bytes");
        CHECK(memcmp(buf, src, 100) == 0, "short inline answer: the bytes");
        CHECK(buf[100] == 0x5A, "short inline answer: nothing past the reply");
        CHECK(ufs_feof(fp), "short inline answer: EOF");
        CHECK_EQ(g_ncall, 1, "short inline answer: no further request");
        close_file(&fp);
    }

    /* 4K answer: data through buf_ptr, data_len 4; short is EOF */
    fp = open_file(ufs, "r");
    if (fp) {
        memset(src, 'e', 100);
        begin();
        s_read_buf(src, 100U);
        memset(buf, 0x5A, sizeof(buf));
        CHECK_EQ(ufs_fread(buf, 1, 300, fp), 100, "4K answer, short: 100 bytes");
        CHECK(memcmp(buf, src, 100) == 0, "4K answer, short: stored by the router");
        CHECK(ufs_feof(fp), "4K answer, short: EOF");
        close_file(&fp);
    }

    /* a count with no data to an inline ask cannot be an answer */
    fp = open_file(ufs, "r");
    if (fp) {
        STEP *st;

        begin();
        st = s_resp(4U);
        put_u32(st->data, 10U);
        memset(buf, 0x5A, sizeof(buf));
        CHECK_EQ(ufs_fread(buf, 1, 10, fp), 0, "count without data: nothing delivered");
        CHECK(ufs_ferror(fp) != 0, "count without data: error, not data");
        close_file(&fp);
    }

    close_session(&ufs);
}

int
main(void)
{
    printf("=== TSTLIBUF: libufs through a stubbed SSI ===\n");

    test_stub_conditions();
    test_wire();
    test_transport_failure();
    test_router_refusal();
    test_server_error();
    test_short_response();
    test_success();
    test_error_propagation();
    test_write_pool_empty();
    test_path_limit();
    test_read_pool_empty();

    CHECK_EQ(g_badssob, 0, "every SSOB/SSIB/UFSSSOB libufs built was well formed");

    return mbt_test_summary("TSTLIBUF");
}
