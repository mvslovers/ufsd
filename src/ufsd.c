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
    unsigned    count;

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

    /* Allow up to 5 queued CIBs (CIBSTART at startup + 4 concurrent MODIFYs) */
    __cibset(5);

    wtof("UFSD000I UFSD Filesystem Server %s starting", VERSION);
    wtof("UFSD001I UFSD ready");

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

        /* Wait for next console event.
        ** High bit on the pointer marks this as the last (only) entry.
        ** If ECB is already posted WAIT returns immediately.
        ** Fall back to 1-second timed wait if comecbpt is not available.
        */
        count = 0;
        if (com->comecbpt) {
            ecblist[0] = (unsigned *)((unsigned)com->comecbpt | 0x80000000U);
            ecblist[1] = NULL;
            count = 1;
        }
        if (count) {
            __asm__("WAIT ECBLIST=(%0)" : : "r"(ecblist));
        } else {
            __asm__("STIMER WAIT,BINTVL==F'100'");
        }
    }

    ufsd_shutdown(&ufsd);
    return 0;
}
