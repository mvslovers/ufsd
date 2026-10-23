/* UFSD#RQC.C - compare and swap on a request queue word (#159)
**
** The one machine-dependent step under ufsd_rq_push / ufsd_rq_pop.
**
** S/370 CS R1,R3,D2(B2): compare memory[D2+B2] with R1; if equal, store R3
** into memory (CC 0, R1 unchanged); if unequal, load memory into R1 (CC 1,
** memory unchanged).
**
** No writable static: UFSD and UFSDSSIR are AC(1) (constraint 7).
*/

#include "ufsd.h"

union rq_word {
    UFSREQ   *ptr;
    unsigned  bits;
};

int
ufsd_rq_cs(UFSREQ **word, UFSREQ **expect, UFSREQ *new_val)
{
    union rq_word old;
    union rq_word nw;

    old.ptr = *expect;
    nw.ptr  = new_val;

    /* "memory": keep the chain-word accesses of the caller on their side
    ** of the swap, in the asm itself and not only at the call boundary. */
    __asm__ __volatile__(
        "CS %0,%2,%1"
        : "+r"(old.bits), "+m"(*word)
        : "r"(nw.bits)
        : "cc", "memory");

    if (old.ptr == *expect)
        return 0;                       /* swapped */

    *expect = old.ptr;
    return 1;
}
