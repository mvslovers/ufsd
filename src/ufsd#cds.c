/* UFSD#CDS.C - compare double and swap on a UFSPOOL (#142)
**
** The one machine-dependent step under ufsd_pool_pop / ufsd_pool_push.
** CDS compares the register pair R1/R1+1 with the doubleword at D2(B2); if
** equal it stores the pair R3/R3+1 there (CC 0), else it loads the
** doubleword into R1/R1+1 (CC 1).  R1 and R3 must be even registers: a
** 64-bit operand gives cc370 such a pair (checked in the listing, cc370 -S),
** and the doubleword is head in the even and tag in the odd register.
**
** No writable static: UFSD and UFSDSSIR are AC(1) (CLAUDE.md, 7).
*/

#include "ufsd.h"

int
ufsd_pool_cds(UFSPOOL *pool, UFSPOOL *expect, void *new_head, unsigned new_tag)
{
    union {
        struct { void *head; unsigned tag; } w;
        unsigned long long                   dw;
    } old, nw;

    old.w.head = expect->head;
    old.w.tag  = expect->tag;
    nw.w.head  = new_head;
    nw.w.tag   = new_tag;

    /* "memory": keep the chain-word accesses of the caller on their side
    ** of the swap, in the asm itself and not only at the call boundary. */
    __asm__ __volatile__(
        "CDS %0,%2,%1"
        : "+r"(old.dw), "+m"(*pool)
        : "r"(nw.dw)
        : "cc", "memory");

    if (old.w.head == expect->head && old.w.tag == expect->tag)
        return 0;                       /* swapped */

    expect->head = old.w.head;
    expect->tag  = old.w.tag;
    return 1;
}
