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
#include <clibos.h>
#include <clibwto.h>

static void cmd_stats(UFSD_STC *ufsd);
static void cmd_help(UFSD_STC *ufsd);
static void cmd_shutdown(UFSD_STC *ufsd);
static void cmd_sessions(UFSD_STC *ufsd);
static void cmd_mount(UFSD_STC *ufsd, const char *args);
static void cmd_unmount(UFSD_STC *ufsd, const char *args);
static void cmd_trace(UFSD_STC *ufsd, const char *args);
static void cmd_rebuild(UFSD_STC *ufsd);

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
        } else if (strcmp(buf, "SESSIONS") == 0) {
            cmd_sessions(ufsd);
        } else if (strcmp(buf, "HELP") == 0) {
            cmd_help(ufsd);
        } else if (strcmp(buf, "SHUTDOWN") == 0) {
            cmd_shutdown(ufsd);
        } else if (strncmp(buf, "MOUNT ", 6) == 0) {
            cmd_mount(ufsd, buf + 6);
        } else if (strncmp(buf, "UNMOUNT ", 8) == 0) {
            cmd_unmount(ufsd, buf + 8);
        } else if (strncmp(buf, "TRACE ", 6) == 0) {
            cmd_trace(ufsd, buf + 6);
        } else if (strcmp(buf, "REBUILD") == 0) {
            cmd_rebuild(ufsd);
        } else {
            wtof("UFSD021E Unknown command: %s", buf);
            wtof("UFSD020I Commands: STATS, SESSIONS, MOUNT, UNMOUNT, TRACE, REBUILD, HELP, SHUTDOWN");
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
    UFSD_ANCHOR *anchor;
    unsigned     i;

    anchor = ufsd->anchor;

    wtof("UFSD010I STATUS: %s",
         (ufsd->flags & UFSD_ACTIVE) ? "ACTIVE" : "INACTIVE");

    if (anchor) {
        wtof("UFSD011I CSA FREE REQS:  %u/%u",
             anchor->free_count, anchor->total_reqs);
        wtof("UFSD012I CSA FREE BUFS:  %u/%u",
             anchor->buf_count, anchor->buf_total);
        wtof("UFSD013I TRACE ENTRIES:  %u", anchor->trace_size);
        wtof("UFSD014I REQUESTS SERVED: %u", anchor->stat_requests);
        wtof("UFSD015I ERRORS:          %u", anchor->stat_errors);
        wtof("UFSD017I POSTS SAVED:     %u", anchor->stat_posts_saved);
    }

    for (i = 0; i < ufsd->ndisks; i++) {
        UFSD_DISK *d = ufsd->disks[i];  /* C89: top-of-block decl */
        if (!d) continue;
        wtof("UFSD018I DISK %u (%.8s): freeblk=%u/%u freeinode=%u/%u",
             i, d->ddname,
             d->sb.nfreeblock, d->sb.total_freeblock,
             d->sb.nfreeinode, d->sb.total_freeinode);
    }
}

static void
cmd_sessions(UFSD_STC *ufsd)
{
    ufsd_sess_list(ufsd->anchor);
}

static void
cmd_help(UFSD_STC *ufsd)
{
    (void)ufsd;
    wtof("UFSD020I Commands: STATS, SESSIONS, MOUNT, UNMOUNT, TRACE, REBUILD, HELP, SHUTDOWN");
    wtof("UFSD020I   MOUNT DD=ddname,PATH=/path  |  MOUNT LIST");
    wtof("UFSD020I   UNMOUNT PATH=/path");
    wtof("UFSD020I   TRACE ON|OFF|DUMP");
    wtof("UFSD020I   REBUILD  -- scan inodes, rebuild free block cache");
}

static void
cmd_shutdown(UFSD_STC *ufsd)
{
    ufsd->flags &= ~UFSD_ACTIVE;
    ufsd->flags |=  UFSD_QUIESCE;
}

/* ============================================================
** cmd_mount
**
** AP-1f: Parse "MOUNT DD=ddname,PATH=/path" and call
** ufsd_disk_mount().  All text has been uppercased by caller.
** ============================================================ */
static void
cmd_mount(UFSD_STC *ufsd, const char *args)
{
    const char *p;
    const char *q;
    char        ddname[9];
    char        mountpath[128];
    int         namelen;
    int         pathlen;
    unsigned    i;

    if (!args || !*args) {
        wtof("UFSD021E MOUNT: syntax: MOUNT DD=ddname,PATH=/path  or  MOUNT LIST");
        return;
    }

    /* MOUNT LIST -- show all mounted filesystems */
    if (strcmp(args, "LIST") == 0) {
        wtof("UFSD068I %u filesystem(s) mounted:", ufsd->ndisks);
        for (i = 0; i < ufsd->ndisks; i++) {
            wtof("UFSD069I   %-8s  PATH=%-32s  DSN=%s",
                 ufsd->disks[i]->ddname,
                 ufsd->disks[i]->mountpath[0]
                     ? ufsd->disks[i]->mountpath : "(none)",
                 ufsd->disks[i]->dsn);
        }
        return;
    }

    /* Parse DD=xxx */
    p = strstr(args, "DD=");
    if (!p) {
        wtof("UFSD021E MOUNT: DD= not found");
        return;
    }
    p += 3;
    q = p;
    namelen = 0;
    while (*q && *q != ',' && namelen < 8) {
        ddname[namelen++] = *q++;
    }
    /* Blank-pad to 8 chars */
    while (namelen < 8)
        ddname[namelen++] = ' ';
    ddname[8] = '\0';

    /* Parse PATH=xxx */
    p = strstr(args, "PATH=");
    if (!p) {
        wtof("UFSD021E MOUNT: PATH= not found");
        return;
    }
    p += 5;
    pathlen = 0;
    while (*p && pathlen < 127)
        mountpath[pathlen++] = *p++;
    mountpath[pathlen] = '\0';

    if (pathlen == 0) {
        wtof("UFSD021E MOUNT: PATH is empty");
        return;
    }

    ufsd_disk_mount(ufsd, ddname, mountpath);
}

/* ============================================================
** cmd_unmount
**
** AP-1f: Parse "UNMOUNT PATH=/path" and call ufsd_disk_umount().
** ============================================================ */
static void
cmd_unmount(UFSD_STC *ufsd, const char *args)
{
    const char *p;
    char        mountpath[128];
    int         pathlen;

    if (!args || !*args) {
        wtof("UFSD021E UNMOUNT: syntax: UNMOUNT PATH=/path");
        return;
    }

    /* Allow "PATH=/x" or just "/x" */
    p = strstr(args, "PATH=");
    if (p)
        p += 5;
    else
        p = args;

    pathlen = 0;
    while (*p && pathlen < 127)
        mountpath[pathlen++] = *p++;
    mountpath[pathlen] = '\0';

    if (pathlen == 0) {
        wtof("UFSD021E UNMOUNT: PATH is empty");
        return;
    }

    ufsd_disk_umount(ufsd, mountpath);
}

/* ============================================================
** cmd_trace
**
** AP-1f: Handle "TRACE ON", "TRACE OFF", "TRACE DUMP".
** ============================================================ */
static void
cmd_trace(UFSD_STC *ufsd, const char *args)
{
    UFSD_ANCHOR     *anchor;
    unsigned char    savekey;

    anchor = ufsd->anchor;

    if (strcmp(args, "ON") == 0) {
        if (anchor) {
            if (!__super(PSWKEY0, &savekey)) {
                anchor->flags |= UFSD_ANCHOR_TRACE_ON;
                __prob(savekey, NULL);
            }
        }
        wtof("UFSD083I Trace enabled");
    } else if (strcmp(args, "OFF") == 0) {
        if (anchor) {
            if (!__super(PSWKEY0, &savekey)) {
                anchor->flags &= ~UFSD_ANCHOR_TRACE_ON;
                __prob(savekey, NULL);
            }
        }
        wtof("UFSD083I Trace disabled");
    } else if (strcmp(args, "DUMP") == 0) {
        ufsd_trace_dump(anchor);
    } else {
        wtof("UFSD021E TRACE: syntax: TRACE ON|OFF|DUMP");
    }
}

/* ============================================================
** cmd_rebuild
**
** Scan all inodes on all mounted disks and rebuild the free
** block cache.  Recovers from a broken V7 chain.
** ============================================================ */
static void
cmd_rebuild(UFSD_STC *ufsd)
{
    unsigned i;
    int      rc;

    if (ufsd->ndisks == 0) {
        wtof("UFSD071W REBUILD: no disks mounted");
        return;
    }

    for (i = 0; i < ufsd->ndisks; i++) {
        UFSD_DISK *d = ufsd->disks[i];
        if (!d) continue;
        if (d->flags & UFSD_DISK_RDONLY) {
            wtof("UFSD072I REBUILD: skipping %.8s (read-only)", d->ddname);
            continue;
        }
        wtof("UFSD073I REBUILD: scanning %.8s ...", d->ddname);
        rc = ufsd_sb_rebuild_free(d);
        if (rc == UFSD_RC_OK)
            wtof("UFSD074I REBUILD: %.8s done, freeblk=%u freeinode=%u",
                 d->ddname, d->sb.nfreeblock, d->sb.nfreeinode);
        else
            wtof("UFSD075E REBUILD: %.8s failed RC=%d",
                 d->ddname, rc);
    }
}
