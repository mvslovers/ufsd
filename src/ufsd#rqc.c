/* UFSD#RQC.C - the machine-dependent steps under the request queue (#159)
**
** ufsd_rq_lock / ufsd_rq_unlock: the spin lock that serialises the
** clients' appends.
**
** S/370 CS R1,R3,D2(B2): compare memory[D2+B2] with R1; if equal, store R3
** into memory (CC 0, R1 unchanged); if unequal, load memory into R1 (CC 1,
** memory unchanged).  We try to swap 0 (unlocked) for 1 (locked) and retry
** until the old value comes back as 0.
**
** ufsd_rq_window: nothing on the target.  The host test puts a second "CPU"
** here, between the server's two updates of the queue words.
**
** No writable static: UFSD and UFSDSSIR are AC(1) (CLAUDE.md, 7).
*/

#include "ufsd.h"

void
ufsd_rq_lock(UFSD_ANCHOR *anchor)
{
    unsigned old;

    do {
        old = 0;
        __asm__ __volatile__(
            "CS %0,%2,%1"
            : "+r"(old), "+m"(anchor->req_lock)
            : "r"(1U)
            : "cc");
    } while (old != 0);
}

void
ufsd_rq_unlock(UFSD_ANCHOR *anchor)
{
    anchor->req_lock = 0;
}

void
ufsd_rq_window(void)
{
}
