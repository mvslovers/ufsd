/* UFSDTST.C - UFSD File Operations Test Client
**
** AP-1e: Full end-to-end test of file system operations via SSI.
**
** Test sequence:
**   1. SESS_OPEN              -- open session, receive token
**   2. MKDIR /test            -- create test directory
**   3. FOPEN /test/hello.txt  -- create file for writing
**   4. FWRITE "Hello, World!" -- write 13 bytes
**   5. FCLOSE                 -- close write handle
**   6. FOPEN /test/hello.txt  -- open file for reading
**   7. FREAD 13 bytes         -- read and verify content
**   8. FCLOSE                 -- close read handle
**   9. REMOVE /test/hello.txt -- remove the file
**  10. RMDIR /test            -- remove the directory
**  11. SESS_CLOSE             -- close session
**
** Expected output on success:
**   UFSTST01I Session opened, token=0x00010001
**   UFSTST10I MKDIR /test: OK
**   UFSTST11I FOPEN /test/hello.txt (write): fd=0
**   UFSTST12I FWRITE 13 bytes: written=13
**   UFSTST13I FCLOSE write handle: OK
**   UFSTST14I FOPEN /test/hello.txt (read): fd=0
**   UFSTST15I FREAD 13 bytes: read=13
**   UFSTST16I FREAD data: Hello, World!
**   UFSTST17I FCLOSE read handle: OK
**   UFSTST18I REMOVE /test/hello.txt: OK
**   UFSTST19I RMDIR /test: OK
**   UFSTST02I Session closed
*/

#include <string.h>
#include <clibecb.h>
#include <clibos.h>
#include <clibwto.h>
#include <iefssobh.h>
#include <iefjssib.h>
#include "ufsd.h"

/* ============================================================
** issue_request
**
** Build SSOB + SSIB, call iefssreq, return R15.
** On success (R15==0), fills ufsssob->rc and ufsssob->data.
** ============================================================ */
static int
issue_request(UFSSSOB *ufsssob)
{
    SSOB    ssob;
    SSIB    ssib;
    ECB     ecb;
    int     r15;

    memset(&ssib, 0, sizeof(ssib));
    memcpy(ssib.SSIBID, "SSIB", 4);
    ssib.SSIBLEN = (unsigned short)sizeof(ssib);
    memcpy(ssib.SSIBSSNM, "UFSD", 4);

    ecb = 0;
    memset(&ssob, 0, sizeof(ssob));
    memcpy(ssob.SSOBID, SSOBID_EYE, 4);
    ssob.SSOBLEN  = (unsigned short)sizeof(ssob);
    ssob.SSOBFUNC = (unsigned short)UFSD_SSOBFUNC;
    ssob.SSOBSSIB = &ssib;
    ssob.SSOBRETN = 0;
    ssob.SSOBINDV = ufsssob;

    r15 = iefssreq(&ssob);
    return r15;
}

/* ============================================================
** send_path_req
**
** Helper: send a request whose only parameter is a path string.
** Used for MKDIR, RMDIR, REMOVE.
** ============================================================ */
static int
send_path_req(unsigned func, unsigned token, const char *path)
{
    UFSSSOB ufsssob;
    unsigned pathlen;

    pathlen = strlen(path);

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = func;
    ufsssob.token    = token;
    ufsssob.data_len = pathlen + 1U;
    memcpy(ufsssob.data, path, pathlen + 1U);

    if (issue_request(&ufsssob) != SSRTOK) return -1;
    return ufsssob.rc;
}

int
main(int argc, char **argv)
{
    UFSSSOB  ufsssob;
    unsigned token;
    int      fd;
    int      rc;
    int      r15;
    unsigned bytes_written;
    unsigned bytes_read;
    unsigned mode;
    char     verify_buf[32];
    const char *testdata = "Hello, World!";
    unsigned    testlen  = 13U;

    (void)argc;

    /* APF authorization required for iefssreq */
    rc = clib_apf_setup(argv[0]);
    if (rc) {
        wtof("UFSTST09E APF setup failed RC=%d", rc);
        return 8;
    }

    /* ============================================================
    ** Step 1: SESS_OPEN
    ** ============================================================ */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = UFSREQ_SESS_OPEN;
    ufsssob.token    = 0;
    ufsssob.data_len = 0;

    r15 = issue_request(&ufsssob);
    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E SESS_OPEN failed: R15=%d RC=%d", r15, ufsssob.rc);
        return 8;
    }
    if (ufsssob.data_len < (unsigned)sizeof(unsigned)) {
        wtof("UFSTST09E SESS_OPEN: no token in response");
        return 8;
    }
    token = *(unsigned *)ufsssob.data;
    wtof("UFSTST01I Session opened, token=0x%08X", token);

    /* ============================================================
    ** Step 2: MKDIR /test
    ** ============================================================ */
    rc = send_path_req(UFSREQ_MKDIR, token, "/test");
    if (rc != UFSD_RC_OK) {
        wtof("UFSTST09E MKDIR /test failed: RC=%d", rc);
        goto close_sess;
    }
    wtof("UFSTST10I MKDIR /test: OK");

    /* ============================================================
    ** Step 3: FOPEN /test/hello.txt for writing
    ** ============================================================ */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = UFSREQ_FOPEN;
    ufsssob.token    = token;
    mode             = UFSD_OPEN_WRITE;
    *(unsigned *)ufsssob.data = mode;
    memcpy(ufsssob.data + 4, "/test/hello.txt", 16);
    ufsssob.data_len = 4U + 16U;  /* mode + path with NUL */

    r15 = issue_request(&ufsssob);
    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E FOPEN write failed: R15=%d RC=%d", r15, ufsssob.rc);
        goto rmdir_test;
    }
    fd = *(int *)ufsssob.data;
    wtof("UFSTST11I FOPEN /test/hello.txt (write): fd=%d", fd);

    /* ============================================================
    ** Step 4: FWRITE "Hello, World!" (13 bytes)
    ** ============================================================ */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func               = UFSREQ_FWRITE;
    ufsssob.token              = token;
    *(unsigned *)ufsssob.data  = (unsigned)fd;
    *(unsigned *)(ufsssob.data + 4) = testlen;
    memcpy(ufsssob.data + 8, testdata, testlen);
    ufsssob.data_len = 8U + testlen;

    r15 = issue_request(&ufsssob);
    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E FWRITE failed: R15=%d RC=%d", r15, ufsssob.rc);
        /* try to close the fd anyway */
        memset(&ufsssob, 0, sizeof(ufsssob));
        memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
        ufsssob.func              = UFSREQ_FCLOSE;
        ufsssob.token             = token;
        *(unsigned *)ufsssob.data = (unsigned)fd;
        ufsssob.data_len          = 4U;
        issue_request(&ufsssob);
        goto remove_file;
    }
    bytes_written = *(unsigned *)ufsssob.data;
    wtof("UFSTST12I FWRITE %u bytes: written=%u", testlen, bytes_written);

    /* ============================================================
    ** Step 5: FCLOSE write handle
    ** ============================================================ */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func              = UFSREQ_FCLOSE;
    ufsssob.token             = token;
    *(unsigned *)ufsssob.data = (unsigned)fd;
    ufsssob.data_len          = 4U;

    r15 = issue_request(&ufsssob);
    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E FCLOSE write failed: R15=%d RC=%d", r15, ufsssob.rc);
        goto remove_file;
    }
    wtof("UFSTST13I FCLOSE write handle: OK");

    /* ============================================================
    ** Step 6: FOPEN /test/hello.txt for reading
    ** ============================================================ */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func              = UFSREQ_FOPEN;
    ufsssob.token             = token;
    mode                      = UFSD_OPEN_READ;
    *(unsigned *)ufsssob.data = mode;
    memcpy(ufsssob.data + 4, "/test/hello.txt", 16);
    ufsssob.data_len = 4U + 16U;

    r15 = issue_request(&ufsssob);
    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E FOPEN read failed: R15=%d RC=%d", r15, ufsssob.rc);
        goto remove_file;
    }
    fd = *(int *)ufsssob.data;
    wtof("UFSTST14I FOPEN /test/hello.txt (read): fd=%d", fd);

    /* ============================================================
    ** Step 7: FREAD up to testlen bytes
    ** ============================================================ */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func              = UFSREQ_FREAD;
    ufsssob.token             = token;
    *(unsigned *)ufsssob.data = (unsigned)fd;
    *(unsigned *)(ufsssob.data + 4) = testlen;
    ufsssob.data_len = 8U;

    r15 = issue_request(&ufsssob);
    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E FREAD failed: R15=%d RC=%d", r15, ufsssob.rc);
        /* close read handle */
        memset(&ufsssob, 0, sizeof(ufsssob));
        memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
        ufsssob.func              = UFSREQ_FCLOSE;
        ufsssob.token             = token;
        *(unsigned *)ufsssob.data = (unsigned)fd;
        ufsssob.data_len          = 4U;
        issue_request(&ufsssob);
        goto remove_file;
    }
    bytes_read = *(unsigned *)ufsssob.data;
    wtof("UFSTST15I FREAD %u bytes: read=%u", testlen, bytes_read);

    /* Copy data (not NUL-terminated yet) and verify */
    memset(verify_buf, 0, sizeof(verify_buf));
    if (bytes_read > sizeof(verify_buf) - 1U)
        bytes_read = sizeof(verify_buf) - 1U;
    memcpy(verify_buf, ufsssob.data + 4, bytes_read);
    verify_buf[bytes_read] = '\0';
    wtof("UFSTST16I FREAD data: %s", verify_buf);

    if (strcmp(verify_buf, testdata) != 0)
        wtof("UFSTST09E VERIFY FAILED: expected '%s' got '%s'",
             testdata, verify_buf);

    /* ============================================================
    ** Step 8: FCLOSE read handle
    ** ============================================================ */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func              = UFSREQ_FCLOSE;
    ufsssob.token             = token;
    *(unsigned *)ufsssob.data = (unsigned)fd;
    ufsssob.data_len          = 4U;

    r15 = issue_request(&ufsssob);
    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK)
        wtof("UFSTST09E FCLOSE read failed: R15=%d RC=%d", r15, ufsssob.rc);
    else
        wtof("UFSTST17I FCLOSE read handle: OK");

    /* ============================================================
    ** Step 9: REMOVE /test/hello.txt
    ** ============================================================ */
remove_file:
    rc = send_path_req(UFSREQ_REMOVE, token, "/test/hello.txt");
    if (rc != UFSD_RC_OK)
        wtof("UFSTST09E REMOVE /test/hello.txt failed: RC=%d", rc);
    else
        wtof("UFSTST18I REMOVE /test/hello.txt: OK");

    /* ============================================================
    ** Step 10: RMDIR /test
    ** ============================================================ */
rmdir_test:
    rc = send_path_req(UFSREQ_RMDIR, token, "/test");
    if (rc != UFSD_RC_OK)
        wtof("UFSTST09E RMDIR /test failed: RC=%d", rc);
    else
        wtof("UFSTST19I RMDIR /test: OK");

    /* ============================================================
    ** Step 11: SESS_CLOSE
    ** ============================================================ */
close_sess:
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = UFSREQ_SESS_CLOSE;
    ufsssob.token    = token;
    ufsssob.data_len = 0;

    r15 = issue_request(&ufsssob);
    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E SESS_CLOSE failed: R15=%d RC=%d", r15, ufsssob.rc);
        return 8;
    }
    wtof("UFSTST02I Session closed");
    return 0;
}
