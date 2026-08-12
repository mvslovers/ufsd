/* UFSD#ASV.C - ASVT membership scan (issue #53)
**
** The arithmetic half of the reclaim liveness guard (see
** src/ufsd#rcl.c).  Portable C: no MVS headers, no control block
** navigation, no EBCDIC -- so `make test-host` can run it natively
** (test/mvs/tstufsav.c).
**
** The scan compares ASVT entries against an ASCB address and reads
** nothing out of the ASCB itself.  That is deliberate: the pointer it
** validates comes from CSA and may be stale, and SQA storage of a
** terminated address space can be reused.  Comparing addresses cannot
** fault and needs no ASCB field offsets; reading a jobname out of a
** reused block would answer with another address space's data.
*/

#include "ufsdasv.h"

int
ufsd_ascb_in_asvt(const unsigned *entries, unsigned nentries, unsigned ascb)
{
    unsigned i;

    if (!entries || nentries == 0U || ascb == 0U)
        return 0;

    for (i = 0; i < nentries; i++) {
        /* Available entry: no address space, and the word carries the
        ** next available entry's address -- never an ASCB's. */
        if (entries[i] & UFSD_ASVT_AVAIL)
            continue;
        if (entries[i] == ascb)
            return 1;
    }

    return 0;
}
