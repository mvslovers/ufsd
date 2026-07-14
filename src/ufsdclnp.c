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
**
** Quiescing (#39): UFSDCLNP mirrors the clean-/P path.  It nulls the
** SSVT entry and clears UFSD_ANCHOR_ACTIVE, then DRAINS anchor->inflight
** to zero -- keeping the eye catcher and the router module present -- so
** a client still parked in UFSDSSIR bails RC_CORRUPT on its next timeout
** and leaves the module before we free it.  Freeing the router out from
** under a parked client is an S0C4 in that client's address space (see
** issue #39); the drain closes that window.  It is best-effort: a count
** still standing after the ceiling is almost certainly leaked (a client
** that faulted or whose AS was cancelled while in-flight never runs its
** decrement), so we warn and free anyway -- UFSDCLNP has no fallback but
** an IPL, and stranding CSA is exactly what it exists to prevent.
*/

#include "ufsd.h"
#include <string.h>
#include <clibos.h>
#include <clibwto.h>
#include <clibssct.h>
#include <clibssvt.h>

/* Drain tuning -- see ufsd.c ufsd_drain().  The ceiling exceeds
** 2 * UFSD_WAIT_INTERVAL (2 * 5.00 s) so a client genuinely parked in the
** router gets two wake-and-bail chances before we conclude the count is
** leaked and free anyway. */
#define UFSDCLNP_DRAIN_POLL    10U   /* 0.10 s per poll                    */
#define UFSDCLNP_DRAIN_MAX    120U   /* 120 * 0.10 s = 12.00 s ceiling     */
#define UFSDCLNP_DRAIN_SETTLE  20U   /* 0.20 s after inflight reaches zero */

/* Block for `hsec` hundredths on a private stack ECB nobody posts -- the
** interval timer wakes us.  Same primitive as ufsd_pause() in the STC. */
static void
ufsdclnp_pause(unsigned hsec)
{
    ECB local = 0;
    ecb_timed_wait(&local, hsec, 0);
}

/* Poll anchor->inflight to zero.  Returns 1 if it drained, 0 on ceiling.
** Runs in problem state: inflight is in CSA without fetch-protection and
** another address space decrements it (volatile: reload every poll). */
static int
ufsdclnp_drain(UFSD_ANCHOR *anchor)
{
    volatile unsigned *inflight = &anchor->inflight;
    unsigned           n;

    for (n = 0; n < UFSDCLNP_DRAIN_MAX; n++) {
        if (*inflight == 0) {
            ufsdclnp_pause(UFSDCLNP_DRAIN_SETTLE);
            return (*inflight == 0);      /* re-check: catch a racing exit */
        }
        ufsdclnp_pause(UFSDCLNP_DRAIN_POLL);
    }
    return 0;
}

int
main(int argc, char **argv)
{
    SSCT           *ssct;
    SSVT           *ssvt;
    UFSD_ANCHOR    *anchor;
    unsigned char   savekey;
    int             anchor_ok;
    int             rc;

    (void)argc;

    wtof("UFSD140I UFSDCLNP STARTING");

    /* --- APF authorization (required for key-0 and SSCT ops) --- */
    rc = clib_apf_setup(argv[0]);
    if (rc) {
        wtof("UFSD141E UFSDCLNP: APF SETUP FAILED RC=%d", rc);
        return 8;
    }

    /* --- Locate UFSD subsystem --- */
    ssct = ssct_find("UFSD");
    if (!ssct) {
        wtof("UFSD142I UFSDCLNP: SUBSYSTEM UFSD NOT REGISTERED");
        return 0;
    }

    ssvt      = ssct->ssctssvt;
    anchor    = (UFSD_ANCHOR *)ssct->ssctsuse;
    anchor_ok = (anchor && memcmp(anchor->eye, "UFSDANCR", 8) == 0);

    /* --- Step 1: close the door and arm the parked-client bail ---
    ** Null the SSVT entry so no NEW iefssreq call dispatches into UFSDSSIR,
    ** and clear UFSD_ANCHOR_ACTIVE so a client already parked in the router
    ** bails RC_CORRUPT on its next timeout.  Keep the eye catcher and the
    ** router module present: the router decrements inflight ONLY on the
    ** "eye still valid" bail path, so clearing the eye now would strand the
    ** counter and defeat the drain in step 2. */
    if (__super(PSWKEY0, &savekey)) {
        wtof("UFSD143E UFSDCLNP: CANNOT ENTER SUPERVISOR STATE");
        return 8;
    }
    if (ssvt) {
        ssvt_reset(ssvt, UFSD_SSVT_ROUTER);
        ssvt_funcmap(ssvt, 0, UFSD_SSOBFUNC);
        wtof("UFSD144I UFSDCLNP: SSVT FUNCTION POINTER CLEARED");
    }
    if (anchor_ok)
        anchor->flags &= ~UFSD_ANCHOR_ACTIVE;
    __prob(savekey, NULL);

    /* --- Step 2: drain in-flight clients out of the router (problem
    ** state) --- parked clients need up to one UFSD_WAIT_INTERVAL to notice
    ** the cleared flag and bail, decrementing inflight on the way out.
    ** Best-effort: a count still standing after the ceiling is treated as
    ** leaked and freed anyway (see file header). */
    if (anchor_ok && anchor->inflight) {
        if (!ufsdclnp_drain(anchor))
            wtof("UFSD152W UFSDCLNP: %u CLIENT(S) STILL IN FLIGHT AFTER "
                 "DRAIN -- ASSUMING LEAKED, FREEING", anchor->inflight);
    }

    /* --- Step 3: tear down under key-0 -- deregister the SSCT (frees the
    ** SSVT), unload the router module, release the pools, then the anchor.
    ** Order matters: the module references the pool blocks, so it goes
    ** first; the anchor goes last. */
    if (__super(PSWKEY0, &savekey)) {
        wtof("UFSD143E UFSDCLNP: CANNOT ENTER SUPERVISOR STATE");
        return 8;
    }

    ssct_remove(ssct);
    ssct_free(ssct);
    if (ssvt) ssvt_free(ssvt);
    wtof("UFSD145I UFSDCLNP: SSCT DEREGISTERED");

    if (anchor_ok) {
        /* Free UFSDSSIR CSA load module */
        if (anchor->ssir_lpa) {
            freemain(anchor->ssir_lpa);
            anchor->ssir_lpa = NULL;
            wtof("UFSD146I UFSDCLNP: SSI ROUTER MODULE FREED");
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

        wtof("UFSD147I UFSDCLNP: CSA POOLS FREED");

        /* Invalidate the eye catcher BEFORE the FREEMAIN so a client still
        ** in the drain settle window bails on eye-mismatch instead of
        ** trusting a live-looking anchor.  Anchor goes last. */
        memset(anchor->eye, 0, sizeof(anchor->eye));
        freemain(anchor);
        wtof("UFSD148I UFSDCLNP: ANCHOR FREED");
    } else if (anchor) {
        wtof("UFSD153W UFSDCLNP: ANCHOR EYE MISMATCH AT %08X",
             (unsigned)anchor);
    } else {
        wtof("UFSD154I UFSDCLNP: NO ANCHOR (SSCTSUSE=NULL)");
    }

    __prob(savekey, NULL);

    wtof("UFSD149I UFSDCLNP COMPLETE");
    return 0;
}
