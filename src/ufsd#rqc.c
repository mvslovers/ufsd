/* UFSD#RQC.C - compare and swap on a request queue word (#159)
**
** The one machine-dependent step under ufsd_rq_push / ufsd_rq_pop.
**
** S/370 CS R1,R3,D2(B2): compare memory[D2+B2] with R1; if equal, store R3
** into memory (CC 0, R1 unchanged); if unequal, load memory into R1 (CC 1,
** memory unchanged).
**
** No writable static: UFSD and UFSDSSIR are AC(1) (CLAUDE.md, 7).
*/

#include "ufsd.h"

int
ufsd_rq_cs(UFSREQ **word, UFSREQ **expect, UFSREQ *new_val)
{
    unsigned old = (unsigned)*expect;

    /* "memory": keep the chain-word accesses of the caller on their side
    ** of the swap, in the asm itself and not only at the call boundary. */
    __asm__ __volatile__(
        "CS %0,%2,%1"
        : "+r"(old), "+m"(*word)
        : "r"((unsigned)new_val)
        : "cc", "memory");

    if (old == (unsigned)*expect)
        return 0;                       /* swapped */

    *expect = (UFSREQ *)old;
    return 1;
}
