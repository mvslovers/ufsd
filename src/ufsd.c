/* UFSD.C - UFSD Filesystem Server -- STC Main Program
**
** AP-1a: STC skeleton.
**   - Console interface (CIB/QEDIT)
**   - MODIFY command dispatch
**   - Clean STOP handling
**   - No CSA, no UFS, no subsystem yet
*/

#ifndef VERSION
#define VERSION "1.0.0-dev"
#endif

#include "ufsd.h"
#include <string.h>
#include <clibwto.h>

void
ufsd_shutdown(UFSD_STC *ufsd)
{
    (void)ufsd;
    wtof("UFSD099I UFSD shutdown complete");
}

int
main(int argc, char **argv)
{
    UFSD_STC    ufsd;
    COM         *com;
    CIB         *cib;
    unsigned    *ecblist[2]; /* WAIT ECBLIST: 1 entry, high bit = last */

    (void)argc;
    (void)argv;

    memset(&ufsd, 0, sizeof(ufsd));
    memcpy(ufsd.eye, "**UFSD**", 8);
    ufsd.flags = UFSD_ACTIVE;

    com = __gtcom();
    if (!com) {
        wtof("UFSD090E Unable to initialize console interface");
        return 8;
    }

    /* Allow 1 queued MODIFY command at a time */
    __cibset(1);

    wtof("UFSD000I UFSD Filesystem Server %s starting", VERSION);
    wtof("UFSD001I UFSD ready");

    while (ufsd.flags & UFSD_ACTIVE) {
        /* Check if console ECB was posted (MODIFY or STOP arrived) */
        if (*com->comecbpt & 0x40000000U) {
            cib = __cibget();
            if (cib) {
                ufsd_process_cib(&ufsd, cib);
                __cibdel(cib);
            }
        }

        if (!(ufsd.flags & UFSD_ACTIVE)) break;

        /* Wait for next console event.
        ** High bit on the pointer marks this as the last (only) entry.
        ** If ECB is already posted WAIT returns immediately.
        */
        ecblist[0] = (unsigned *)((unsigned)com->comecbpt | 0x80000000U);
        ecblist[1] = NULL; /* not read by WAIT, but avoids wild pointer */
        __asm__("WAIT ECBLIST=(%0)" : : "r"(ecblist));
    }

    ufsd_shutdown(&ufsd);
    return 0;
}
