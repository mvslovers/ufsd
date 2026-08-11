/* UFSD#RCL.C - Orphaned Predecessor Reclaim (issue #49)
**
** Shared teardown for a UFSD instance that terminated without freeing
** its CSA footprint.  After an abend the ESTAE path deliberately frees
** nothing (see ufsd.c): the SSCT stays chained, the SSI router module
** and the pools stay in CSA.  Both callers run the same MVS-verified
** sequence (originally in ufsdclnp.c, moved here):
**
**   UFSD startup   main() reclaims BEFORE registering, so after /C a
**                  plain /S UFSD suffices: no second SSCT chained, no
**                  CSA leak.  force=0 -- a predecessor whose anchor is
**                  still flagged ACTIVE is treated as a running server
**                  and left untouched.
**   UFSDCLNP       standalone emergency utility.  force=1 -- reclaims
**                  unconditionally (its historical semantics).
**
** Quiescing (#39): null the SSVT entry and clear UFSD_ANCHOR_ACTIVE,
** then DRAIN anchor->inflight to zero -- keeping the eye catcher and
** the router module present -- so a client still parked in UFSDSSIR
** bails RC_CORRUPT on its next timeout and leaves the module before we
** free it.  Freeing the router out from under a parked client is an
** S0C4 in that client's address space (see issue #39); the drain
** closes that window.  It is best-effort: a count still standing after
** the ceiling is almost certainly leaked (a client that faulted or
** whose AS was cancelled while in-flight never runs its decrement), so
** we warn and free anyway -- the alternative is stranding the CSA
** until an IPL.
*/

#include "ufsd.h"
#include <string.h>
#include <clibos.h>
#include <clibwto.h>

/* Drain tuning -- see ufsd.c ufsd_drain().  The ceiling exceeds
** 2 * UFSD_WAIT_INTERVAL (2 * 5.00 s) so a client genuinely parked in the
** router gets two wake-and-bail chances before we conclude the count is
** leaked and free anyway. */
#define UFSD_RCL_DRAIN_POLL    10U   /* 0.10 s per poll                    */
#define UFSD_RCL_DRAIN_MAX    120U   /* 120 * 0.10 s = 12.00 s ceiling     */
#define UFSD_RCL_DRAIN_SETTLE  20U   /* 0.20 s after inflight reaches zero */

/* Block for `hsec` hundredths on a private stack ECB nobody posts -- the
** interval timer wakes us.  Same primitive as ufsd_pause() in ufsd.c. */
static void
ufsd_rcl_pause(unsigned hsec)
{
    ECB local = 0;
    ecb_timed_wait(&local, hsec, 0);
}

/* Poll anchor->inflight to zero.  Returns 1 if it drained, 0 on ceiling.
** Runs in problem state: inflight is in CSA without fetch-protection and
** another address space decrements it (volatile: reload every poll). */
static int
ufsd_rcl_drain(UFSD_ANCHOR *anchor)
{
    volatile unsigned *inflight = &anchor->inflight;
    unsigned           n;

    for (n = 0; n < UFSD_RCL_DRAIN_MAX; n++) {
        if (*inflight == 0) {
            ufsd_rcl_pause(UFSD_RCL_DRAIN_SETTLE);
            return (*inflight == 0);      /* re-check: catch a racing exit */
        }
        ufsd_rcl_pause(UFSD_RCL_DRAIN_POLL);
    }
    return 0;
}

/* ============================================================
** ufsd_reclaim
**
** Find a registered UFSD_SSNAME SSCT and reclaim it: quiesce, drain,
** deregister, free all CSA.  Caller must be APF authorized and
** in a normal task environment (timed WAITs; not under RTM).
**
** Returns UFSD_RECLAIM_NONE / _DONE / _ACTIVE / _FAIL.
** ============================================================ */
int
ufsd_reclaim(int force)
{
    SSCT           *ssct;
    SSVT           *ssvt;
    UFSD_ANCHOR    *anchor;
    unsigned char   savekey;
    int             anchor_ok;

    ssct = ssct_find(UFSD_SSNAME);
    if (!ssct)
        return UFSD_RECLAIM_NONE;

    ssvt      = ssct->ssctssvt;
    anchor    = (UFSD_ANCHOR *)ssct->ssctsuse;
    anchor_ok = (anchor && memcmp(anchor->eye, "UFSDANCR", 8) == 0);

    /* Liveness gate.  Every path out of a UFSD instance -- clean /P,
    ** drain-timeout retain, and the ESTAE -- clears UFSD_ANCHOR_ACTIVE
    ** before the STC ends, so a set flag means a running server (or one
    ** whose ESTAE never got to run, e.g. after FORCE).  Tearing that
    ** down would yank the CSA from under live clients; without `force`,
    ** hands off.  An SSCT without a valid anchor cannot be a live
    ** server -- reclaim it. */
    if (!force && anchor_ok && (anchor->flags & UFSD_ANCHOR_ACTIVE))
        return UFSD_RECLAIM_ACTIVE;

    /* --- Step 1: close the door and arm the parked-client bail ---
    ** Null the SSVT entry so no NEW iefssreq call dispatches into UFSDSSIR,
    ** and clear UFSD_ANCHOR_ACTIVE so a client already parked in the router
    ** bails RC_CORRUPT on its next timeout.  Keep the eye catcher and the
    ** router module present: the router decrements inflight ONLY on the
    ** "eye still valid" bail path, so clearing the eye now would strand the
    ** counter and defeat the drain in step 2. */
    if (__super(PSWKEY0, &savekey)) {
        wtof("UFSD143E RECLAIM: CANNOT ENTER SUPERVISOR STATE");
        return UFSD_RECLAIM_FAIL;
    }
    if (ssvt) {
        ssvt_reset(ssvt, UFSD_SSVT_ROUTER);
        ssvt_funcmap(ssvt, 0, UFSD_SSOBFUNC);
        wtof("UFSD144I RECLAIM: SSVT FUNCTION POINTER CLEARED");
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
        if (!ufsd_rcl_drain(anchor))
            wtof("UFSD152W RECLAIM: %u CLIENT(S) STILL IN FLIGHT AFTER "
                 "DRAIN -- ASSUMING LEAKED, FREEING", anchor->inflight);
    }

    /* --- Step 3: tear down under key-0 -- deregister the SSCT (frees the
    ** SSVT), unload the router module, release the pools, then the anchor.
    ** Order matters: the module references the pool blocks, so it goes
    ** first; the anchor goes last. */
    if (__super(PSWKEY0, &savekey)) {
        wtof("UFSD143E RECLAIM: CANNOT ENTER SUPERVISOR STATE");
        return UFSD_RECLAIM_FAIL;
    }

    ssct_remove(ssct);
    ssct_free(ssct);
    if (ssvt) ssvt_free(ssvt);
    wtof("UFSD145I RECLAIM: SSCT DEREGISTERED");

    if (anchor_ok) {
        /* Free UFSDSSIR CSA load module */
        if (anchor->ssir_lpa) {
            freemain(anchor->ssir_lpa);
            anchor->ssir_lpa = NULL;
            wtof("UFSD146I RECLAIM: SSI ROUTER MODULE FREED");
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

        wtof("UFSD147I RECLAIM: CSA POOLS FREED");

        /* Invalidate the eye catcher BEFORE the FREEMAIN so a client still
        ** in the drain settle window bails on eye-mismatch instead of
        ** trusting a live-looking anchor.  Anchor goes last. */
        memset(anchor->eye, 0, sizeof(anchor->eye));
        freemain(anchor);
        wtof("UFSD148I RECLAIM: ANCHOR FREED");
    } else if (anchor) {
        wtof("UFSD153W RECLAIM: ANCHOR EYE MISMATCH AT %08X",
             (unsigned)anchor);
    } else {
        wtof("UFSD154I RECLAIM: NO ANCHOR (SSCTSUSE=NULL)");
    }

    __prob(savekey, NULL);

    return UFSD_RECLAIM_DONE;
}
