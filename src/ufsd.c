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
**   - Any abend triggers emergency shutdown (SSCT deregistered,
**     SSI router unloaded) then percolates for normal MVS dump
**   - ufsd_shutdown() deletes the ESTAE as its first action to
**     prevent re-entrant recovery on clean or emergency shutdown
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
**   1. Deregister the SSCT + unload UFSDSSIR so that MVS can
**      route future SSI calls to another handler (or reject them
**      cleanly) rather than crashing in a dangling SSVT entry.
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

    wtof("UFSD098E UFSD abend intercepted -- emergency shutdown");

    if (ufsd) {
        ufsd->flags &= ~UFSD_ACTIVE;
        ufsd_shutdown(ufsd);
    }

    /* Percolate: let MVS produce the abend dump and terminate */
    sdwa->SDWARCDE = SDWACWT;
}

void
ufsd_shutdown(UFSD_STC *ufsd)
{
    UFSD_ANCHOR *anchor;

    /* Delete ESTAE first: prevents re-entrant recovery if shutdown
    ** itself encounters an error (clean path or emergency path). */
    __estae(ESTAE_DELETE, NULL, NULL);

    anchor = ufsd->anchor;

    /* AP-1d Step 2: close disk datasets (STC-local, before CSA free) */
    ufsd_ufs_term(ufsd);

    if (anchor) {
        /* AP-1c: deregister + unload SSI router first */
        if (anchor->ssir_lpa) {
            ufsd_ssi_unload(anchor);
            wtof("UFSD036I SSI router unloaded");
        }
        if (anchor->ssct) {
            ufsd_ssct_free(anchor);
            wtof("UFSD095I SSCT deregistered");
        }
        /* AP-1e: release GFT before session table */
        if (anchor->gfiles) {
            ufsd_gft_free(anchor);
            wtof("UFSD048I Global file table freed");
        }
        /* AP-1d: release session table before freeing CSA */
        if (anchor->sessions) {
            ufsd_sess_free(anchor);
            wtof("UFSD046I Session table freed");
        }
        ufsd_csa_free(anchor);
        wtof("UFSD096I CSA freed");
        ufsd_anchor_free(anchor);
        ufsd->anchor = NULL;
    }

    wtof("UFSD099I UFSD shutdown complete");
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
        wtof("UFSD090E Unable to initialize console interface");
        return 8;
    }

    /* Allow up to 5 queued CIBs (CIBSTART at startup + 4 MODIFYs) */
    __cibset(5);

    /* --- APF authorization --------------------------------------- */
    rc = clib_apf_setup(argv[0]);
    if (rc) {
        wtof("UFSD091E APF setup failed RC=%d (STEPLIB not APF authorized?)",
             rc);
        return 8;
    }

    /* --- ESTAE recovery ------------------------------------------ */
    __estae(ESTAE_CREATE, ufsd_recover, &ufsd);

    /* --- CSA anchor ---------------------------------------------- */
    anchor = ufsd_anchor_alloc();
    if (!anchor) {
        wtof("UFSD092E Cannot allocate CSA anchor");
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

    wtof("UFSD030I CSA allocated: Anchor=%08X", (unsigned)anchor);
    wtof("UFSD031I   Request Pool: %u blocks, %uK",
         (unsigned)UFSD_REQ_POOL_COUNT,
         (UFSD_REQ_POOL_COUNT * (unsigned)sizeof(UFSREQ) + 511U) / 1024U);
    wtof("UFSD032I   Buffer Pool:  %u blocks, %uK",
         (unsigned)UFSD_BUF_POOL_COUNT,
         (UFSD_BUF_POOL_COUNT * (unsigned)sizeof(UFSBUF) + 511U) / 1024U);
    wtof("UFSD033I   Trace Buffer: %u entries, %uK",
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
    wtof("UFSD034I SSCT registered, subsystem name=UFSD");

    /* --- SSI router ------------------------------------------ */
    rc = ufsd_ssi_load(anchor);
    if (rc) {
        ufsd_ssct_free(anchor);
        ufsd_csa_free(anchor);
        ufsd_anchor_free(anchor);
        ufsd.anchor = NULL;
        return 8;
    }
    wtof("UFSD035I SSI router loaded at %08X", (unsigned)anchor->ssir_lpa);

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
    wtof("UFSD045I Session table: %u slots", (unsigned)UFSD_MAX_SESSIONS);

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
    wtof("UFSD047I Global file table: %u slots", (unsigned)UFSD_MAX_GFILES);

    /* --- UFS disk init (AP-1d Step 2) ----------------------------- */
    ufsd_ufs_init(&ufsd); /* returns 0 even with zero disks */

    wtof("UFSD000I UFSD Filesystem Server %s starting", VERSION);
    wtof("UFSD001I UFSD ready");

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

    ufsd_shutdown(&ufsd);
    return 0;
}
