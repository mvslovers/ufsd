/* UFSDPING.C - UFSD Subsystem Ping Test Client
**
** AP-1c: Batch program to verify the SSI round-trip.
**
** Builds an SSOB + SSIB + UFSSSOB, calls iefssreq(), and
** issues WTOs with the result.
**
** Expected output on success:
**   UFSPING1I Sending PING to UFSD subsystem...
**   UFSPING2I UFSD responded: RC=0 (PONG)
**
** Expected output on failure (STC not running, etc.):
**   UFSPING1I Sending PING to UFSD subsystem...
**   UFSPING3E IEFSSREQ failed: R15=%d SSOBRETN=%d
*/

#include <string.h>
#include <clibecb.h>
#include <clibos.h>
#include <clibwto.h>
#include <iefssobh.h>
#include <iefjssib.h>
#include "ufsd.h"

int
main(int argc, char **argv)
{
    SSOB    ssob;
    SSIB    ssib;
    UFSSSOB ufsssob;
    ECB     ping_ecb;
    int     r15;

    /* crent370's iefssreq calls MODESET (SVC 107) internally,
    ** which requires APF authorization.  Establish it here. */
    r15 = clib_apf_setup(argv[0]);
    if (r15) {
        wtof("UFSPING9E APF setup failed RC=%d", r15);
        return 8;
    }

    /* --- Build SSIB --- */
    memset(&ssib, 0, sizeof(ssib));
    memcpy(ssib.SSIBID, "SSIB", 4);
    ssib.SSIBLEN = (unsigned short)sizeof(ssib);
    memcpy(ssib.SSIBSSNM, "UFSD", 4);

    /* Client-private ECB: UFSDSSIR WAITs on this; server cross-AS POSTs it */
    ping_ecb = 0;

    /* --- Build UFSSSOB extension --- */
    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func       = UFSREQ_PING;
    ufsssob.token      = 0;
    ufsssob.client_ecb = &ping_ecb;
    ufsssob.data_len   = 0;

    /* --- Build SSOB header --- */
    memset(&ssob, 0, sizeof(ssob));
    memcpy(ssob.SSOBID, SSOBID_EYE, 4);
    ssob.SSOBLEN  = (unsigned short)sizeof(ssob);
    ssob.SSOBFUNC = (unsigned short)UFSD_SSOBFUNC;
    ssob.SSOBSSIB = &ssib;
    ssob.SSOBRETN = 0;
    ssob.SSOBINDV = &ufsssob;

    wtof("UFSPING1I Sending PING to UFSD subsystem...");

    r15 = iefssreq(&ssob);

    if (r15 == SSRTOK && ufsssob.rc == UFSD_RC_OK) {
        wtof("UFSPING2I UFSD responded: RC=0 (PONG)");
        return 0;
    }

    wtof("UFSPING3E IEFSSREQ failed: R15=%d SSOBRETN=%d RC=%d",
         r15, ssob.SSOBRETN, ufsssob.rc);
    return 8;
}
