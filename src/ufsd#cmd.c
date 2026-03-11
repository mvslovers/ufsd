/* UFSD#CMD.C - Operator MODIFY command handler
**
** AP-1a: STATS, HELP, SHUTDOWN
** AP-1b: STATS extended (CSA pool counts)
** AP-1d: SESSIONS
** AP-1e: STATS extended (open files)
** AP-1f: MOUNT, UNMOUNT, TRACE ON|OFF|DUMP
*/

#include "ufsd.h"
#include <string.h>
#include <ctype.h>
#include <clibwto.h>

static void cmd_stats(UFSD_STC *ufsd);
static void cmd_help(UFSD_STC *ufsd);
static void cmd_shutdown(UFSD_STC *ufsd);

int
ufsd_process_cib(UFSD_STC *ufsd, CIB *cib)
{
    char    buf[64];
    int     len;
    int     i;

    switch (cib->cibverb) {

    case CIBMODFY:
        len = (int)cib->cibdatln;
        if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
        memcpy(buf, cib->cibdata, (size_t)len);
        buf[len] = '\0';

        /* Uppercase and trim trailing spaces */
        for (i = 0; i < len; i++) {
            buf[i] = (char)toupper((unsigned char)buf[i]);
        }
        while (len > 0 && buf[len - 1] == ' ') {
            buf[--len] = '\0';
        }

        if (strcmp(buf, "STATS") == 0) {
            cmd_stats(ufsd);
        } else if (strcmp(buf, "HELP") == 0) {
            cmd_help(ufsd);
        } else if (strcmp(buf, "SHUTDOWN") == 0) {
            cmd_shutdown(ufsd);
        } else {
            wtof("UFSD021E Unknown command: %s", buf);
            wtof("UFSD020I Commands: STATS, HELP, SHUTDOWN");
        }
        break;

    case CIBSTOP:
        /* /P UFSD */
        cmd_shutdown(ufsd);
        break;

    default:
        break;
    }

    return 0;
}

static void
cmd_stats(UFSD_STC *ufsd)
{
    wtof("UFSD010I STATUS: %s",
         (ufsd->flags & UFSD_ACTIVE) ? "ACTIVE" : "INACTIVE");
    /*
    ** AP-1b will add:
    **   UFSD011I CSA FREE REQS:  xx/32
    **   UFSD012I CSA FREE BUFS:  xx/16
    **   UFSD013I TRACE ENTRIES:  256
    ** AP-1d will add:
    **   UFSD014I REQUESTS SERVED: xxx
    **   UFSD015I ERRORS:          xxx
    */
}

static void
cmd_help(UFSD_STC *ufsd)
{
    (void)ufsd;
    wtof("UFSD020I Commands: STATS, HELP, SHUTDOWN");
}

static void
cmd_shutdown(UFSD_STC *ufsd)
{
    ufsd->flags &= ~UFSD_ACTIVE;
    ufsd->flags |=  UFSD_QUIESCE;
}
