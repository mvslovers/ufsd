/* UFSDCLNP.C - Emergency Cleanup After UFSD Abend
**
** Standalone batch program that deregisters the UFSD subsystem
** and frees all CSA storage.  Run this instead of an IPL after
** a UFSD STC abend leaves orphaned CSA blocks and a dangling
** SSCT entry.
**
** Usage:  //CLEANUP EXEC PGM=UFSDCLNP
**         //STEPLIB  DD  DSN=your.ufsd.load,DISP=SHR
**
** Requires APF authorization (AC=1 in linkedit, APF STEPLIB).
**
** Since #49 the reclaim sequence lives in ufsd_reclaim()
** (src/ufsd#rcl.c) and UFSD startup runs it itself, so after an abend
** a plain /S UFSD suffices.  UFSDCLNP stays the standalone fallback:
** emergencies, older load libraries, and the case startup refuses --
** an anchor flagged ACTIVE whose server ASCB cannot be checked.
**
** It reclaims with force=1, which since #53 no longer means
** "unconditionally": a UFSD whose address space is still in the ASVT
** is off limits here too.  Any UFSD_RECLAIM_ACTIVE reaching this
** program therefore means a PROVEN live server -- force already covers
** the undecidable case -- so the refusal below can say so plainly.
** There is no override: stop UFSD first (/P, or FORCE if it hangs).
**
** Recovery cycle after UFSD abend:
**   /S UFSD           (reclaims the orphan itself, then starts)
** or standalone:
**   /S UFSDCLNP       (30 seconds, replaces 15-minute IPL)
**   /S UFSD
**   /S HTTPD
*/

#include "ufsd.h"
#include <clibos.h>
#include <clibwto.h>

int
main(int argc, char **argv)
{
    int rc;

    (void)argc;

    wtof("UFSD140I UFSDCLNP STARTING");

    /* --- APF authorization (required for key-0 and SSCT ops) --- */
    rc = clib_apf_setup(argv[0]);
    if (rc) {
        wtof("UFSD141E UFSDCLNP: APF SETUP FAILED RC=%d", rc);
        return 8;
    }

    rc = ufsd_reclaim(1);
    if (rc == UFSD_RECLAIM_NONE) {
        wtof("UFSD142I UFSDCLNP: SUBSYSTEM %s NOT REGISTERED", UFSD_SSNAME);
        return 0;
    }
    if (rc == UFSD_RECLAIM_ACTIVE) {
        /* UFSD155W named the address space.  Nothing was touched. */
        wtof("UFSD157W UFSDCLNP: CLEANUP SUPPRESSED -- "
             "STOP UFSD FIRST (/P UFSD)");
        return 8;
    }
    if (rc != UFSD_RECLAIM_DONE)
        return 8;                         /* UFSD143E already issued */

    wtof("UFSD149I UFSDCLNP COMPLETE");
    return 0;
}
