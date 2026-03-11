/* UFSD#SCT.C - Subsystem Control Table Registration
**
** AP-1b: Allocate SSVT + SSCT in CSA, install in the JESCT chain.
** AP-1c: Load UFSDSSIR into CSA, register in SSVT.
*/

#include "ufsd.h"
#include <clibos.h>
#include <clibwto.h>

/* ============================================================
** ufsd_ssct_init
**
** Allocate SSVT and SSCT in CSA, chain the SSCT into JESCT,
** and store the pointers in the anchor for later cleanup.
**
** ssctsuse is set to the anchor address so the SSI router
** (AP-1c) can recover the anchor from a given SSCT.
**
** Returns 0 on success, -1 on failure (WTO issued on error).
** ============================================================ */
int
ufsd_ssct_init(UFSD_ANCHOR *anchor)
{
    unsigned char   savekey;
    SSVT           *ssvt;
    SSCT           *ssct;
    int             rc;

    if (__super(PSWKEY0, &savekey)) {
        wtof("UFSD091E Cannot enter supervisor state for SSCT init");
        return -1;
    }

    ssvt = ssvt_new(UFSREQ_MAX);
    if (!ssvt) {
        __prob(savekey, NULL);
        wtof("UFSD093E Cannot allocate SSVT");
        return -1;
    }

    ssct = ssct_new("UFSD", ssvt, (void *)anchor);
    if (!ssct) {
        ssvt_free(ssvt);
        __prob(savekey, NULL);
        wtof("UFSD094E Cannot allocate SSCT");
        return -1;
    }

    rc = ssct_install(ssct, NULL);
    if (rc) {
        ssct_free(ssct);
        ssvt_free(ssvt);
        __prob(savekey, NULL);
        wtof("UFSD097E Cannot install SSCT, RC=%d", rc);
        return -1;
    }

    anchor->ssct = ssct;
    anchor->ssvt = ssvt;

    __prob(savekey, NULL);
    return 0;
}

/* ============================================================
** ufsd_ssi_load  (AP-1c)
**
** Load the UFSDSSIR module from STEPLIB into CSA (SP=241) using
** __loadhi(), then register its entry point in the SSVT so that
** IEFSSREQ routes UFSD_SSOBFUNC requests to it.
**
** Must be called after ufsd_ssct_init() (SSVT must exist).
** Must be called in supervisor state with key 0.
** Returns 0 on success, -1 on failure (WTO issued).
** ============================================================ */
int
ufsd_ssi_load(UFSD_ANCHOR *anchor)
{
    unsigned char   savekey;
    void           *lpa;
    void           *epa;
    unsigned        size;
    int             rc;

    if (!anchor || !anchor->ssvt) return -1;

    if (__super(PSWKEY0, &savekey)) {
        wtof("UFSD098E Cannot enter supervisor for SSI load");
        return -1;
    }

    rc = __loadhi("UFSDSSIR", &lpa, &epa, &size);
    if (rc) {
        __prob(savekey, NULL);
        wtof("UFSD098E Cannot load UFSDSSIR into CSA, RC=%d", rc);
        return -1;
    }

    ssvt_set(anchor->ssvt, UFSD_SSVT_ROUTER, epa);
    ssvt_funcmap(anchor->ssvt, UFSD_SSVT_ROUTER, UFSD_SSOBFUNC);

    anchor->ssir_lpa  = lpa;
    anchor->ssir_size = size;

    __prob(savekey, NULL);
    return 0;
}

/* ============================================================
** ufsd_ssi_unload  (AP-1c)
**
** Deregister the router from the SSVT so no new requests are
** dispatched, then freemain the UFSDSSIR CSA block.
** Must be called before ufsd_ssct_free().
** ============================================================ */
void
ufsd_ssi_unload(UFSD_ANCHOR *anchor)
{
    unsigned char savekey;

    if (!anchor || !anchor->ssir_lpa) return;
    if (__super(PSWKEY0, &savekey)) return;

    if (anchor->ssvt) {
        ssvt_reset(anchor->ssvt, UFSD_SSVT_ROUTER);
        ssvt_funcmap(anchor->ssvt, 0, UFSD_SSOBFUNC); /* un-map func */
    }

    freemain(anchor->ssir_lpa);
    anchor->ssir_lpa  = NULL;
    anchor->ssir_size = 0;

    __prob(savekey, NULL);
}

/* ============================================================
** ufsd_ssct_free
**
** Remove the SSCT from the JESCT chain and free SSCT + SSVT.
** Nulls the anchor fields so double-free is safe.
** ============================================================ */
void
ufsd_ssct_free(UFSD_ANCHOR *anchor)
{
    unsigned char savekey;

    if (!anchor || !anchor->ssct) return;
    if (__super(PSWKEY0, &savekey)) return;

    ssct_remove(anchor->ssct);
    ssct_free(anchor->ssct);
    ssvt_free(anchor->ssvt);

    anchor->ssct = NULL;
    anchor->ssvt = NULL;

    __prob(savekey, NULL);
}
