/* UFSD.C - UFSD Filesystem Server -- STC Main Program
**
** AP-1a: STC skeleton.
**   - Console interface (CIB/QEDIT)
**   - MODIFY command dispatch
**   - Clean STOP handling
**
** AP-1b: CSA infrastructure + SSCT registration.
**   - APF authorization (clib_apf_setup)
**   - UFSD_ANCHOR in CSA (SP=241)
**   - Request pool, buffer pool, trace ring buffer
**   - SSCT chained into JESCT
**   - Pools freed and SSCT deregistered on STOP
**
** AP-1c: SSI router + first round-trip.
**   - UFSDSSIR loaded into CSA via __loadhi
**   - SSVT entry registered for UFSD_SSOBFUNC
**   - Main loop drains request queue + dispatches
**   - WAIT on both console ECB and server_ecb
**
** ESTAE recovery (AP-1d+):
**   - ufsd_recover() registered immediately after APF setup
**   - ufsd_shutdown() deletes the ESTAE as its first action to
**     prevent re-entrant recovery on clean or emergency shutdown
**
** Shutdown quiescing (Issue #30):
**   ufsd_shutdown() nulls the SSVT function entry and clears
**   UFSD_ANCHOR_ACTIVE FIRST, so iefssreq stops routing new clients
**   into UFSDSSIR and parked clients bail on their next timeout.
**   - UFSD_SHUT_NORMAL (clean STOP / startup fail): then drains the
**     anchor->inflight counter to zero and releases the CSA.  A drain
**     timeout retains CSA rather than freeing storage a client may
**     still be executing.
**   - UFSD_SHUT_ABEND (ESTAE): nulls the entry + clears the flag only,
**     then percolates for the MVS dump.  Under RTM the state of
**     in-flight clients is unknown, so nothing is freed.  UFSDCLNP
**     reclaims the CSA on the next start.
*/

#ifndef VERSION
#define VERSION "1.0.0-dev"
#endif

#include "ufsd.h"
#include <string.h>
#include <clibos.h>
#include <clibwto.h>
#include <clibstae.h>

/* ============================================================
** ufsd_recover
**
** ESTAE recovery routine.  Called by MVS on any unhandled abend
** in the UFSD STC address space.
**
** Goals:
**   1. Null the SSVT function entry + clear UFSD_ANCHOR_ACTIVE
**      (UFSD_SHUT_ABEND) so iefssreq stops dispatching new clients
**      into UFSDSSIR and parked clients bail on their next timeout.
**      CSA is NOT freed: under RTM a foreign PSW may still be inside
**      the router or the pools, and freeing it is the very S0C4 we
**      are recovering from.  UFSDCLNP reclaims the CSA on next start.
**   2. Percolate (SDWACWT = 0): MVS produces the SVC dump and
**      terminates the address space normally.
**
** SDWAPARM holds &ufsd (the STC block on main's stack), set via
** __estae(ESTAE_CREATE, ufsd_recover, &ufsd).
**
** ufsd_shutdown() deletes the ESTAE as its first action, so
** this routine is never called re-entrantly.
** ============================================================ */
static void
ufsd_recover(SDWA *sdwa)
{
    UFSD_STC *ufsd;

    if (!sdwa) return;

    ufsd = (UFSD_STC *)sdwa->SDWAPARM;

    wtof("UFSD098E UFSD ABEND INTERCEPTED -- EMERGENCY SHUTDOWN");

    if (ufsd) {
        ufsd->flags &= ~UFSD_ACTIVE;
        ufsd_shutdown(ufsd, UFSD_SHUT_ABEND);
    }

    /* Percolate: let MVS produce the abend dump and terminate */
    sdwa->SDWARCDE = SDWACWT;
}

/* ufsd_pause -- block the STC for `hsec` hundredths of a second.
**
** Uses ecb_timed_wait() on a private stack ECB that nobody posts: the
** interval timer fires after `hsec` hundredths, posts the ECB, and wakes
** us.  This is the same timed-wait primitive the SSI router (5 s liveness
** wait) and libc370 sleep() use, so its runtime behaviour is already
** proven on this target -- preferred over a hand-rolled STIMER WAIT,
** BINTVL whose operand form and unit would be unverified here.  It is
** also a call barrier, so ufsd_drain()'s poll of anchor->inflight is
** reloaded across it. */
static void
ufsd_pause(unsigned hsec)
{
    ECB local = 0;
    ecb_timed_wait(&local, hsec, 0);
}

/* ufsd_drain -- poll anchor->inflight to zero.  Returns 1 on success,
** 0 on timeout.
**
** With no clients in flight this returns after one settle -- a clean /P
** costs a fraction of a second.  A client parked in ecb_timed_wait needs
** up to one full UFSD_WAIT_INTERVAL (5.00 s) to notice the cleared
** ANCHOR_ACTIVE flag, so the ceiling exceeds that.
**
** The counter tracks CSA *data* access; a client that has just
** decremented is still executing the router epilogue out of the CSA load
** module, so a short settle is added and inflight re-checked.  Neither is
** a guarantee -- which is why a drain timeout means "retain CSA", not
** "free anyway".  anchor->inflight is in CSA without fetch-protection and
** is read from problem state (volatile: another address space writes it;
** no __super needed to poll). */
#define UFSD_DRAIN_POLL     10U   /* 0.10 s per poll                     */
#define UFSD_DRAIN_MAX     100U   /* 100 * 0.10 s = 10.00 s ceiling      */
#define UFSD_DRAIN_SETTLE   20U   /* 0.20 s after inflight reaches zero  */

static int
ufsd_drain(UFSD_ANCHOR *anchor)
{
    volatile unsigned *inflight = &anchor->inflight;
    unsigned           n;

    for (n = 0; n < UFSD_DRAIN_MAX; n++) {
        if (*inflight == 0) {
            ufsd_pause(UFSD_DRAIN_SETTLE);
            return (*inflight == 0);      /* re-check: catch a racing entry */
        }
        ufsd_pause(UFSD_DRAIN_POLL);
    }
    return 0;
}

void
ufsd_shutdown(UFSD_STC *ufsd, int mode)
{
    UFSD_ANCHOR   *anchor;
    unsigned char  savekey;

    /* Delete ESTAE first: prevents re-entrant recovery if shutdown
    ** itself encounters an error. */
    __estae(ESTAE_DELETE, NULL, NULL);

    anchor = ufsd->anchor;

    if (anchor) {
        /* --- Step 1: close the door -------------------------------
        ** Null the SSVT function entry BEFORE anything is freed.  Until
        ** this store, iefssreq keeps routing new clients into UFSDSSIR --
        ** including into the entry checks themselves, which are
        ** instructions inside the very module we are about to FREEMAIN.
        ** Clearing ANCHOR_ACTIVE alone does not stop that; only the SSVT
        ** entry does.  Same order as UFSDCLNP. */
        if (__super(PSWKEY0, &savekey)) {
            wtof("UFSD097E CANNOT ENTER SUPERVISOR STATE -- "
                 "CSA RETAINED, RUN UFSDCLNP BEFORE RESTART");
            ufsd_ufs_term(ufsd);
            return;                       /* free nothing */
        }
        if (anchor->ssvt) {
            ssvt_reset(anchor->ssvt, UFSD_SSVT_ROUTER);
            ssvt_funcmap(anchor->ssvt, 0, UFSD_SSOBFUNC);
        }
        /* Step 2: parked clients bail on their next timeout. */
        anchor->flags &= ~UFSD_ANCHOR_ACTIVE;
        __prob(savekey, NULL);
    }

    /* Step 3: close disk datasets (STC-local, superblock writeback). */
    ufsd_ufs_term(ufsd);

    if (!anchor)
        goto done;

    if (mode == UFSD_SHUT_ABEND) {
        /* Under RTM we cannot drain and must not free: a foreign PSW may
        ** still be executing inside UFSDSSIR or touching the pools.
        ** Leave everything and percolate; UFSDCLNP reclaims on restart. */
        wtof("UFSD097W ABEND SHUTDOWN -- CSA RETAINED, "
             "RUN UFSDCLNP BEFORE RESTART");
        goto done;
    }

    /* Step 4: drain in-flight clients out of the router. */
    if (!ufsd_drain(anchor)) {
        wtof("UFSD098W %u CLIENT(S) STILL IN FLIGHT -- CSA RETAINED, "
             "RUN UFSDCLNP BEFORE RESTART", anchor->inflight);
        goto done;                        /* free nothing */
    }

    /* Step 5..9: teardown, same order as UFSDCLNP -- deregister the SSCT
    ** (which also frees the SSVT), unload the router module, then release
    ** the tables, pools, and finally the anchor. */
    if (anchor->ssct) {
        ufsd_ssct_free(anchor);
        wtof("UFSD095I SSCT DEREGISTERED");
    }
    if (anchor->ssir_lpa) {
        ufsd_ssi_unload(anchor);
        wtof("UFSD036I SSI ROUTER UNLOADED");
    }
    if (anchor->gfiles) {
        ufsd_gft_free(anchor);
        wtof("UFSD048I GLOBAL FILE TABLE FREED");
    }
    if (anchor->sessions) {
        ufsd_sess_free(anchor);
        wtof("UFSD046I SESSION TABLE FREED");
    }
    ufsd_csa_free(anchor);
    wtof("UFSD096I CSA FREED");
    ufsd_anchor_free(anchor);             /* clears the eye catcher first */
    ufsd->anchor = NULL;

done:
    wtof("UFSD099I UFSD SHUTDOWN COMPLETE");
}

int
main(int argc, char **argv)
{
    UFSD_STC     ufsd;
    UFSD_ANCHOR *anchor;
    COM          *com;
    CIB          *cib;
    UFSREQ      *req;
    unsigned     *ecblist[3]; /* WAIT ECBLIST: up to 2 entries + sentinel */
    unsigned      count;
    int           rc;

    (void)argc;

    memset(&ufsd, 0, sizeof(ufsd));
    memcpy(ufsd.eye, "**UFSD**", 8);
    ufsd.flags = UFSD_ACTIVE;

    /* --- Console interface ---------------------------------------- */
    com = __gtcom();
    if (!com) {
        wtof("UFSD090E UNABLE TO INITIALIZE CONSOLE INTERFACE");
        return 8;
    }

    /* Allow up to 5 queued CIBs (CIBSTART at startup + 4 MODIFYs) */
    __cibset(5);

    /* --- APF authorization --------------------------------------- */
    rc = clib_apf_setup(argv[0]);
    if (rc) {
        wtof("UFSD091E APF SETUP FAILED RC=%d (STEPLIB NOT APF AUTHORIZED?)",
             rc);
        return 8;
    }

    /* --- ESTAE recovery ------------------------------------------ */
    __estae(ESTAE_CREATE, ufsd_recover, &ufsd);

    wtof("UFSD000I UFSD %s STARTING", VERSION);

    /* --- CSA anchor ---------------------------------------------- */
    anchor = ufsd_anchor_alloc();
    if (!anchor) {
        wtof("UFSD092E CANNOT ALLOCATE CSA ANCHOR");
        return 8;
    }
    ufsd.anchor = anchor;

    /* --- CSA pools ----------------------------------------------- */
    rc = ufsd_csa_init(anchor);
    if (rc) {
        ufsd_anchor_free(anchor);
        ufsd.anchor = NULL;
        return 8;
    }

    /* Record STC ASCB and STC pointer in anchor */
    {
        unsigned char savekey;
        if (!__super(PSWKEY0, &savekey)) {
            anchor->server_ascb = __ascb(0);
            anchor->server_stc  = (void *)&ufsd;
            __prob(savekey, NULL);
        }
    }

    wtof("UFSD030I CSA ALLOCATED: ANCHOR=%08X", (unsigned)anchor);
    wtof("UFSD031I   REQUEST POOL: %u BLOCKS, %uK",
         (unsigned)UFSD_REQ_POOL_COUNT,
         (UFSD_REQ_POOL_COUNT * (unsigned)sizeof(UFSREQ) + 511U) / 1024U);
    wtof("UFSD032I   BUFFER POOL:  %u BLOCKS, %uK",
         (unsigned)UFSD_BUF_POOL_COUNT,
         (UFSD_BUF_POOL_COUNT * (unsigned)sizeof(UFSBUF) + 511U) / 1024U);
    wtof("UFSD033I   TRACE BUFFER: %u ENTRIES, %uK",
         (unsigned)UFSD_TRACE_SIZE,
         (UFSD_TRACE_SIZE * (unsigned)sizeof(UFSD_TRACE) + 511U) / 1024U);

    /* --- SSCT registration --------------------------------------- */
    rc = ufsd_ssct_init(anchor);
    if (rc) {
        ufsd_csa_free(anchor);
        ufsd_anchor_free(anchor);
        ufsd.anchor = NULL;
        return 8;
    }
    wtof("UFSD034I SSCT REGISTERED, SUBSYSTEM NAME=UFSD");

    /* --- SSI router ------------------------------------------ */
    rc = ufsd_ssi_load(anchor);
    if (rc) {
        ufsd_ssct_free(anchor);
        ufsd_csa_free(anchor);
        ufsd_anchor_free(anchor);
        ufsd.anchor = NULL;
        return 8;
    }
    wtof("UFSD035I SSI ROUTER LOADED AT %08X", (unsigned)anchor->ssir_lpa);

    /* --- Session table (AP-1d) ----------------------------------- */
    rc = ufsd_sess_init(anchor);
    if (rc) {
        ufsd_ssi_unload(anchor);
        ufsd_ssct_free(anchor);
        ufsd_csa_free(anchor);
        ufsd_anchor_free(anchor);
        ufsd.anchor = NULL;
        return 8;
    }
    wtof("UFSD045I SESSION TABLE: %u SLOTS", (unsigned)UFSD_MAX_SESSIONS);

    /* --- Global file table (AP-1e) --------------------------------- */
    rc = ufsd_gft_init(anchor);
    if (rc) {
        ufsd_sess_free(anchor);
        ufsd_ssi_unload(anchor);
        ufsd_ssct_free(anchor);
        ufsd_csa_free(anchor);
        ufsd_anchor_free(anchor);
        ufsd.anchor = NULL;
        return 8;
    }
    wtof("UFSD047I GLOBAL FILE TABLE: %u SLOTS", (unsigned)UFSD_MAX_GFILES);

    /* --- UFS disk init (AP-1d Step 2) ----------------------------- */
    rc = ufsd_ufs_init(&ufsd);
    if (rc != 0) {
        /* No clients yet: drain sees inflight == 0 and returns at once. */
        ufsd_shutdown(&ufsd, UFSD_SHUT_NORMAL);
        return 8;
    }

    {
        unsigned csa_kb =
            ((UFSD_REQ_POOL_COUNT * (unsigned)sizeof(UFSREQ)) +
             (UFSD_BUF_POOL_COUNT * (unsigned)sizeof(UFSBUF)) +
             (UFSD_TRACE_SIZE * (unsigned)sizeof(UFSD_TRACE)) +
             (unsigned)sizeof(UFSD_ANCHOR) + 1023U) / 1024U;
        wtof("UFSD001I UFSD %s READY -- %u DISKS, %uK CSA, "
             "%u SESSIONS, %u FILES",
             VERSION, ufsd.ndisks, csa_kb,
             (unsigned)UFSD_MAX_SESSIONS, (unsigned)UFSD_MAX_GFILES);
    }

    /* --- Main event loop ----------------------------------------- */
    while (ufsd.flags & UFSD_ACTIVE) {
        /* Drain all pending CIBs unconditionally.
        ** We do NOT gate this on the ECB: at startup MVS may queue a
        ** CIBSTART without posting the ECB, which would otherwise hold
        ** the single CIB slot and cause every subsequent MODIFY to be
        ** rejected with IEE342I TASK BUSY.
        */
        while ((cib = __cibget()) != NULL) {
            ufsd_process_cib(&ufsd, cib);
            __cibdel(cib);
            if (!(ufsd.flags & UFSD_ACTIVE)) break;
        }

        if (!(ufsd.flags & UFSD_ACTIVE)) break;

        /* AP-1c: Drain the request queue.
        ** Use a double-check loop: reset server_ecb, then drain.
        ** If a new request arrived between the last dequeue and the
        ** reset, we catch it in the next iteration before WAIT.
        */
        if (anchor) {
            do {
                while ((req = ufsd_dequeue(anchor)) != NULL) {
                    ufsd_dispatch(anchor, req);
                }
                ufsd_server_ecb_reset(anchor);
            } while (anchor->req_head != NULL);
        }

        if (!(ufsd.flags & UFSD_ACTIVE)) break;

        /* Wait for next console event OR incoming SSI request.
        ** High bit on the LAST pointer marks end of ECBLIST.
        ** If any ECB is already posted, WAIT returns immediately.
        ** Fall back to 1-second timed WAIT if no ECBs are available.
        */
        count = 0;
        if (com->comecbpt && anchor) {
            ecblist[0] = (unsigned *)com->comecbpt;
            ecblist[1] = (unsigned *)((unsigned)&anchor->server_ecb
                                      | 0x80000000U);
            ecblist[2] = NULL;
            count = 2;
        } else if (com->comecbpt) {
            ecblist[0] = (unsigned *)((unsigned)com->comecbpt | 0x80000000U);
            ecblist[1] = NULL;
            count = 1;
        } else if (anchor) {
            ecblist[0] = (unsigned *)((unsigned)&anchor->server_ecb
                                      | 0x80000000U);
            ecblist[1] = NULL;
            count = 1;
        }
        if (count) {
            /* server_ecb is in CSA (key 0); WAIT in supervisor state
            ** so MVS can write the "waiting" bit into the ECB.
            ** An authorised task may WAIT on ECBs in supervisor-key
            ** subpools (OS/VS2 SPLS GC28-0683 §WAIT).              */
            unsigned char savekey;
            if (!__super(PSWKEY0, &savekey)) {
                __asm__("WAIT ECBLIST=(%0)" : : "r"(ecblist));
                __prob(savekey, NULL);
            }
        } else {
            __asm__("STIMER WAIT,BINTVL==F'100'");
        }
    }

    ufsd_shutdown(&ufsd, UFSD_SHUT_NORMAL);   /* clean STOP */
    return 0;
}
