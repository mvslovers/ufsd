/* TSTUFSAV.C - ASVT membership scan tests (issue #53)
**
** Covers the scan the reclaim liveness guard rests on
** (src/ufsd#asv.c).  Dual-target: `make test-host` runs it natively,
** `make test-mvs` runs it as a load module.
**
** The guard decides whether UFSDCLNP tears down the CSA of a running
** UFSD or refuses.  Its dangerous direction is a false "not found":
** that answers "the server is dead", and the caller then frees CSA out
** from under live clients.  Two ways to produce one silently are an
** off-by-one that stops before the last ASVT entry and a high-bit test
** that skips assigned entries instead of available ones -- so both are
** asserted from either side here.
**
** Addresses below are plausible MVS 3.8j SQA values (24-bit, ASCBs
** live low in storage), but the scan is pure comparison, so nothing
** depends on that.
*/

#include <mbtcheck.h>
#include "ufsdasv.h"

#define ASCB_A   0x00F01000U     /* the ASCB we are looking for       */
#define ASCB_B   0x00F02000U     /* some other address space's ASCB   */
#define ASCB_C   0x00F03000U

/* An available entry: the AVAIL bit plus the address of the next
** available entry (an address INSIDE the ASVT, never an ASCB's). */
#define AVAIL(next)  (UFSD_ASVT_AVAIL | (unsigned)(next))

/* ============================================================
** check_position
**
** The same ASCB placed at every index of a table must be found at
** every index.  First and last are the ones that break: an off-by-one
** loop misses index nentries-1, a loop starting at 1 misses ASID 1
** (MASTER, index 0).
** ============================================================ */
static void
check_position(void)
{
    unsigned entries[8];
    char     msg[64];
    unsigned n;
    unsigned i;

    for (n = 0; n < 8U; n++) {
        for (i = 0; i < 8U; i++)
            entries[i] = ASCB_B;
        entries[n] = ASCB_A;

        sprintf(msg, "found at index %u of 8", n);
        CHECK(ufsd_ascb_in_asvt(entries, 8U, ASCB_A), msg);
    }
}

/* ============================================================
** check_bounds
**
** nentries is asvtmaxu and is the whole contract with the caller: an
** entry past it belongs to no address space and must not be read.  A
** scan that over-runs by one would report a dead server as live
** (harmless) -- or, on a table sized from a different field, walk into
** storage that is not the ASVT at all.
** ============================================================ */
static void
check_bounds(void)
{
    unsigned entries[4];

    entries[0] = ASCB_B;
    entries[1] = ASCB_C;
    entries[2] = ASCB_B;
    entries[3] = ASCB_A;                  /* only inside the 4th slot */

    CHECK(ufsd_ascb_in_asvt(entries, 4U, ASCB_A),
          "last entry is scanned");
    CHECK(!ufsd_ascb_in_asvt(entries, 3U, ASCB_A),
          "entry past nentries is not scanned");
    CHECK(!ufsd_ascb_in_asvt(entries, 0U, ASCB_A),
          "empty table finds nothing");
    CHECK(!ufsd_ascb_in_asvt(NULL, 4U, ASCB_A),
          "null table finds nothing");
    CHECK(!ufsd_ascb_in_asvt(entries, 4U, 0U),
          "a null ASCB is never found");
}

/* ============================================================
** check_avail
**
** Available entries carry an address next to the AVAIL bit.  Masking
** it off before the comparison -- or testing the bit the wrong way
** round -- would match a free-chain word against an ASCB address and
** call a dead server live, or skip every assigned entry and call a
** live server dead.  Both are asserted.
** ============================================================ */
static void
check_avail(void)
{
    unsigned entries[4];

    /* The AVAIL bit sits on top of the very address we search for. */
    entries[0] = ASCB_B;
    entries[1] = AVAIL(ASCB_A);
    entries[2] = AVAIL(0);                /* last available entry     */
    entries[3] = ASCB_C;

    CHECK(!ufsd_ascb_in_asvt(entries, 4U, ASCB_A),
          "available entry is not a match, bit or no bit");

    /* Same table, one assigned entry added: still found. */
    entries[2] = ASCB_A;
    CHECK(ufsd_ascb_in_asvt(entries, 4U, ASCB_A),
          "assigned entry next to available ones is found");

    /* Every entry available: nothing is assigned, nothing matches. */
    entries[0] = AVAIL(ASCB_A);
    entries[1] = AVAIL(ASCB_A);
    entries[2] = AVAIL(ASCB_A);
    entries[3] = AVAIL(ASCB_A);
    CHECK(!ufsd_ascb_in_asvt(entries, 4U, ASCB_A),
          "fully available table finds nothing");
}

/* ============================================================
** check_table
**
** One table shaped like a small system: MASTER, JES2, a few started
** tasks, holes where address spaces have ended.  UFSD's ASCB is in
** it; the address one slot below it is not.
** ============================================================ */
static void
check_table(void)
{
    unsigned entries[6];

    entries[0] = 0x00F00000U;             /* ASID 1  MASTER           */
    entries[1] = 0x00F00800U;             /* ASID 2  JES2             */
    entries[2] = AVAIL(0x00000010U);      /* ASID 3  ended            */
    entries[3] = ASCB_A;                  /* ASID 4  UFSD             */
    entries[4] = ASCB_C;                  /* ASID 5  HTTPD            */
    entries[5] = AVAIL(0);                /* ASID 6  never used       */

    CHECK(ufsd_ascb_in_asvt(entries, 6U, ASCB_A),
          "system table: UFSD ASCB found");
    CHECK(!ufsd_ascb_in_asvt(entries, 6U, ASCB_A - 8U),
          "system table: a near miss is not a match");
    CHECK(!ufsd_ascb_in_asvt(entries, 6U, 0x00000010U),
          "system table: a free chain address is not a match");
}

int
main(void)
{
    printf("=== UFSD ASVT membership scan tests ===\n");

    check_position();
    check_bounds();
    check_avail();
    check_table();

    return mbt_test_summary("TSTUFSAV");
}
