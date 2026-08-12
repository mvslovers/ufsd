/* TSTUFSRC.C - Reclaim liveness guard, target side (issue #53)
**
** MVS-only companion to TSTUFSAV.  The host test covers the ASVT scan
** as arithmetic (src/ufsd#asv.c); this one covers what only a real
** system has: the CVT -> ASVT navigation, and the three answers
** ufsd_server_state() (src/ufsd#rcl.c) gives about an anchor's
** server_ascb.
**
** It exists because the operator action that produces the interesting
** branch -- FORCE the STC, leaving UFSD_ANCHOR_ACTIVE set with the
** address space gone -- needs master console authority, so it cannot
** be driven from the mvsMF console the rest of the suite uses.  Feeding
** the guard a hand-built anchor reaches the same code with the same
** control blocks.
**
** The anchor here is a local struct, not CSA: the guard reads exactly
** one field out of it and touches nothing else, so no GETMAIN, no
** key-0, and nothing registered with the system.  A UFSD that happens
** to be running is not affected in any way.
*/

#include <mbtcheck.h>
#include <clibos.h>
#include "ufsd.h"

/* State names for the failure messages -- a bare 0/1/2 in a test log
** costs a trip to the header. */
static const char *
state_name(int state)
{
    switch (state) {
    case UFSD_SRV_DEAD:    return "DEAD";
    case UFSD_SRV_LIVE:    return "LIVE";
    case UFSD_SRV_UNKNOWN: return "UNKNOWN";
    default:               return "?";
    }
}

static void
check_state(const char *what, void *server_ascb, int want)
{
    UFSD_ANCHOR anchor;
    char        msg[96];
    int         got;

    anchor.server_ascb = server_ascb;
    got = ufsd_server_state(&anchor);

    sprintf(msg, "%s -> %s (got %s)", what, state_name(want),
            state_name(got));
    CHECK(got == want, msg);
}

int
main(void)
{
    void *self;
    void *master;

    printf("=== UFSD reclaim liveness guard (target) ===\n");

    self   = __ascb(0);                   /* this address space   */
    master = __ascb(1);                   /* ASID 1 -- always up  */

    printf("  ASCB: self=%08X master=%08X\n", (unsigned)self,
           (unsigned)master);

    /* Nothing to look up: never answer "dead", because the caller
    ** would free the CSA on that answer. */
    check_state("no ASCB recorded", NULL, UFSD_SRV_UNKNOWN);

    /* A live address space that is not us: this is the answer that
    ** stops UFSDCLNP tearing down a running server. */
    CHECK(master != NULL, "MASTER ASCB located via __ascb(1)");
    if (master)
        check_state("MASTER ASCB", master, UFSD_SRV_LIVE);

    /* Addresses no address space owns.  Inside MASTER's own ASCB and
    ** in the PSA -- both are valid storage, so a scan that matched on
    ** anything but an exact ASVT entry would call them live. */
    if (master)
        check_state("inside MASTER's ASCB", (char *)master + 8,
                    UFSD_SRV_DEAD);
    check_state("PSA address", (void *)0x100, UFSD_SRV_DEAD);

    /* Our own ASCB.  A restart handed the predecessor's recycled ASCB
    ** block sees exactly this, and must read it as an orphan's --
    ** otherwise /S UFSD and UFSDCLNP both refuse and only an IPL is
    ** left.  Note this is deliberately NOT the LIVE answer even though
    ** the address IS in the ASVT. */
    CHECK(self != NULL, "own ASCB located via __ascb(0)");
    if (self)
        check_state("own ASCB", self, UFSD_SRV_DEAD);

    /* If UFSD is up while this runs, its anchor is the real thing --
    ** worth asserting that the guard says LIVE about the server that
    ** is serving us.  Skipped (not failed) when nothing is registered:
    ** the suite must pass on a system without the STC. */
    {
        SSCT        *ssct   = ssct_find(UFSD_SSNAME);
        UFSD_ANCHOR *anchor = ssct ? (UFSD_ANCHOR *)ssct->ssctsuse : NULL;

        if (anchor && memcmp(anchor->eye, "UFSDANCR", 8) == 0
            && anchor->server_ascb) {
            printf("  live %s anchor at %08X, server ASCB %08X\n",
                   UFSD_SSNAME, (unsigned)anchor,
                   (unsigned)anchor->server_ascb);
            CHECK(ufsd_server_state(anchor) == UFSD_SRV_LIVE,
                  "registered UFSD anchor reads LIVE");
        } else {
            printf("  no registered %s anchor -- live case skipped\n",
                   UFSD_SSNAME);
        }
    }

    return mbt_test_summary("TSTUFSRC");
}
