/* UFSDTST.C - UFSD Session Lifecycle Test Client
**
** AP-1d Step 1: Test session open and close via SSI round-trip.
**
** Sends UFSREQ_SESS_OPEN, prints the returned token,
** then sends UFSREQ_SESS_CLOSE with that token.
**
** Expected output on success:
**   UFSTST01I Session opened, token=0x00010001
**   UFSTST02I Session closed
**
** Expected output on failure (STC not running, etc.):
**   UFSTST09E SESS_OPEN failed: R15=%d SSOBRETN=%d RC=%d
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
** Build SSOB + SSIB + UFSSSOB, call iefssreq, return R15.
** On success (R15==0), fills ufsssob->rc and ufsssob->data.
** ============================================================ */
static int
issue_request(UFSSSOB *ufsssob)
{
    SSOB    ssob;
    SSIB    ssib;
    ECB     ecb;
    int     r15;

    /* --- Build SSIB --- */
    memset(&ssib, 0, sizeof(ssib));
    memcpy(ssib.SSIBID, "SSIB", 4);
    ssib.SSIBLEN = (unsigned short)sizeof(ssib);
    memcpy(ssib.SSIBSSNM, "UFSD", 4);

    /* --- Build SSOB --- */
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

int
main(int argc, char **argv)
{
    UFSSSOB ufsssob;
    unsigned token;
    int      r15;

    (void)argc;

    /* iefssreq calls MODESET (SVC 107) internally -- requires APF */
    r15 = clib_apf_setup(argv[0]);
    if (r15) {
        wtof("UFSTST09E APF setup failed RC=%d", r15);
        return 8;
    }

    /* --------------------------------------------------------
    ** Step 1: SESS_OPEN
    ** -------------------------------------------------------- */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = UFSREQ_SESS_OPEN;
    ufsssob.token    = 0;
    ufsssob.data_len = 0;

    r15 = issue_request(&ufsssob);

    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E SESS_OPEN failed: R15=%d SSOBRETN=%d RC=%d",
             r15, ufsssob.rc, ufsssob.rc);
        return 8;
    }

    if (ufsssob.data_len < (unsigned)sizeof(unsigned)) {
        wtof("UFSTST09E SESS_OPEN: no token in response (data_len=%u)",
             ufsssob.data_len);
        return 8;
    }

    token = *(unsigned *)ufsssob.data;
    wtof("UFSTST01I Session opened, token=0x%08X", token);

    /* --------------------------------------------------------
    ** Step 2: SESS_CLOSE
    ** -------------------------------------------------------- */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = UFSREQ_SESS_CLOSE;
    ufsssob.token    = token;
    ufsssob.data_len = 0;

    r15 = issue_request(&ufsssob);

    if (r15 != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
        wtof("UFSTST09E SESS_CLOSE failed: R15=%d SSOBRETN=%d RC=%d",
             r15, ufsssob.rc, ufsssob.rc);
        return 8;
    }

    wtof("UFSTST02I Session closed");
    return 0;
}
