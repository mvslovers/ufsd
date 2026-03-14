/* UFSDCLNP.C - Emergency Cleanup After UFSD Abend
**
** Standalone batch program that deregisters the UFSD subsystem
** and frees all CSA storage.  Run this instead of an IPL after
** a UFSD STC abend leaves orphaned CSA blocks and a dangling
** SSCT entry.
**
** Usage:  //CLEANUP EXEC PGM=UFSDCLNP
**         //STEPLIB  DD  DSN=your.ufsd.load,DISP=SHR
**
** Requires APF authorization (AC=1 in linkedit, APF STEPLIB).
**
** Recovery cycle after UFSD abend:
**   /S UFSDCLNP       (30 seconds, replaces 15-minute IPL)
**   /S UFSD
**   /S HTTPD
*/

#include "ufsd.h"
#include <string.h>
#include <clibos.h>
#include <clibwto.h>
#include <clibssct.h>
#include <clibssvt.h>

int
main(int argc, char **argv)
{
    SSCT           *ssct;
    SSVT           *ssvt;
    UFSD_ANCHOR    *anchor;
    unsigned char   savekey;
    int             rc;

    (void)argc;

    wtof("UFSDCLNP starting -- emergency UFSD cleanup");

    /* --- APF authorization (required for key-0 and SSCT ops) --- */
    rc = clib_apf_setup(argv[0]);
    if (rc) {
        wtof("UFSDCLNP APF setup failed RC=%d", rc);
        return 8;
    }

    /* --- Locate UFSD subsystem --- */
    ssct = ssct_find("UFSD");
    if (!ssct) {
        wtof("UFSDCLNP UFSD subsystem not registered -- nothing to do");
        return 0;
    }

    /* --- Enter supervisor state key-0 for all CSA operations --- */
    if (__super(PSWKEY0, &savekey)) {
        wtof("UFSDCLNP cannot enter supervisor state");
        return 8;
    }

    /* --- Step 1: Null the SSVT function pointer immediately ---
    ** This stops any new IEFSSREQ calls from dispatching to the
    ** (now gone) UFSDSSIR module.  In-flight calls that already
    ** entered the router will complete or abend on their own;
    ** the important thing is no NEW calls can start.
    */
    ssvt = ssct->ssctssvt;
    if (ssvt) {
        ssvt_reset(ssvt, UFSD_SSVT_ROUTER);
        ssvt_funcmap(ssvt, 0, UFSD_SSOBFUNC);
        wtof("UFSDCLNP SSVT function pointer cleared");
    }

    /* --- Step 2: Grab anchor before unchaining SSCT --- */
    anchor = (UFSD_ANCHOR *)ssct->ssctsuse;

    /* --- Step 3: Unchain SSCT from JESCT and free SSCT+SSVT --- */
    ssct_remove(ssct);
    ssct_free(ssct);
    if (ssvt) ssvt_free(ssvt);
    wtof("UFSDCLNP SSCT deregistered and freed");

    /* --- Step 4: Free CSA pools if anchor looks valid ---
    ** We check the eye catcher to avoid freeing random storage
    ** if ssctsuse was corrupt or zero.
    **
    ** Order matters: SSI router module first (it references the
    ** pool blocks), then pools, then anchor itself.
    */
    if (anchor && memcmp(anchor->eye, "UFSDANCR", 8) == 0) {
        /* Free UFSDSSIR CSA load module */
        if (anchor->ssir_lpa) {
            freemain(anchor->ssir_lpa);
            anchor->ssir_lpa = NULL;
            wtof("UFSDCLNP SSI router module freed");
        }

        /* Free trace ring buffer */
        if (anchor->trace_buf) {
            freemain(anchor->trace_buf);
            anchor->trace_buf = NULL;
        }

        /* Free 4K buffer pool (contiguous allocation) */
        if (anchor->buf_pool_base) {
            freemain(anchor->buf_pool_base);
            anchor->buf_pool_base = NULL;
            anchor->buf_free      = NULL;
        }

        /* Free request pool (contiguous allocation) */
        if (anchor->req_pool_base) {
            freemain(anchor->req_pool_base);
            anchor->req_pool_base = NULL;
            anchor->free_head     = NULL;
        }

        wtof("UFSDCLNP CSA pools freed (trace + buffers + requests)");

        /* Free anchor itself (must be last) */
        freemain(anchor);
        wtof("UFSDCLNP anchor freed");
    } else if (anchor) {
        wtof("UFSDCLNP anchor eye mismatch at %08X -- skipping CSA free",
             (unsigned)anchor);
    } else {
        wtof("UFSDCLNP no anchor found (ssctsuse=NULL)");
    }

    __prob(savekey, NULL);

    wtof("UFSDCLNP complete -- UFSD can be restarted");
    return 0;
}
