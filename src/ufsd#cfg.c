/* UFSD#CFG.C - Parmlib Configuration Parser
**
** AP-3a: Read and parse SYS1.PARMLIB(UFSDPRMx) via DD:UFSDPRM.
**
** Config syntax (comments delimited by slash-star):
**   ROOT   DSN(SYS1.UFSD.ROOT) SIZE(1M) BLKSIZE(4096)
**   MOUNT  DSN(HTTPD.WEBROOT)  PATH(/www) MODE(RO)
**   MOUNT  DSN(USER.HOME)      PATH(/u/USER) MODE(RW) OWNER(USER)
**
** ufsd_cfg_read   read config from DD:UFSDPRM
** ufsd_cfg_dump   WTO summary of parsed config
*/

#include "ufsd.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <clibwto.h>

/* ============================================================
** parse_param
**
** Extract value from KEY(value) in a line.
** Returns 1 if found, 0 if not.  Copies at most outlen-1 chars.
** ============================================================ */
static int
parse_param(const char *line, const char *key, char *out, unsigned outlen)
{
    const char *p;
    const char *end;
    unsigned    klen;
    unsigned    vlen;

    klen = strlen(key);
    p = line;

    for (;;) {
        p = strstr(p, key);
        if (!p) return 0;
        /* Must be followed by '(' */
        if (p[klen] != '(') { p += klen; continue; }
        /* Must be preceded by whitespace or start of line */
        if (p != line && !isspace((unsigned char)p[-1]))
            { p += klen; continue; }
        break;
    }

    p += klen + 1;  /* skip KEY( */
    end = strchr(p, ')');
    if (!end) return 0;

    vlen = (unsigned)(end - p);
    if (vlen >= outlen) vlen = outlen - 1U;
    memcpy(out, p, vlen);
    out[vlen] = '\0';
    return 1;
}

/* ============================================================
** parse_size
**
** Convert size string to bytes.  Supports K and M suffix.
** "1M" = 1048576, "500K" = 512000, "256" = 256.
** ============================================================ */
static unsigned
parse_size(const char *s)
{
    unsigned val;
    char     suffix;

    if (!s || !*s) return 0;

    val    = 0;
    suffix = '\0';

    while (*s >= '0' && *s <= '9') {
        val = val * 10U + (unsigned)(*s - '0');
        s++;
    }

    suffix = (char)toupper((unsigned char)*s);
    if (suffix == 'K') val *= 1024U;
    else if (suffix == 'M') val *= 1048576U;

    return val;
}

/* ============================================================
** strip_trailing
**
** Remove trailing whitespace in-place.
** ============================================================ */
static void
strip_trailing(char *s)
{
    int i;

    i = (int)strlen(s) - 1;
    while (i >= 0 && isspace((unsigned char)s[i]))
        s[i--] = '\0';
}

/* ============================================================
** ufsd_cfg_read
**
** Read DD:UFSDPRM and parse into UFSD_CONFIG.
** Returns 0 on success, 8 on error.
** ============================================================ */
int
ufsd_cfg_read(UFSD_CONFIG *cfg)
{
    FILE *fp;
    char  line[512];
    int   in_comment;
    char  val[256];
    char *p;

    if (!cfg) return 8;

    memset(cfg, 0, sizeof(*cfg));
    cfg->root_blksize = 4096;

    fp = fopen("DD:UFSDPRM", "r");
    if (!fp) {
        wtof("UFSD100W Cannot open DD:UFSDPRM -- using defaults");
        return 8;
    }

    in_comment = 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        strip_trailing(line);

        /* Handle block comments */
        if (in_comment) {
            if (strstr(line, "*/"))
                in_comment = 0;
            continue;
        }

        /* Skip blank lines */
        p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;

        /* Start of block comment */
        if (p[0] == '/' && p[1] == '*') {
            if (!strstr(p + 2, "*/"))
                in_comment = 1;
            continue;
        }

        /* ROOT statement */
        if (strncmp(p, "ROOT", 4) == 0 && isspace((unsigned char)p[4])) {
            if (parse_param(p, "DSN", val, sizeof(val)))
                strncpy(cfg->root_dsname, val, 44);
            if (parse_param(p, "SIZE", val, sizeof(val)))
                cfg->root_size = parse_size(val);
            if (parse_param(p, "BLKSIZE", val, sizeof(val)))
                cfg->root_blksize = parse_size(val);
            if (cfg->root_blksize == 0)
                cfg->root_blksize = 4096;
            continue;
        }

        /* MOUNT statement */
        if (strncmp(p, "MOUNT", 5) == 0 && isspace((unsigned char)p[5])) {
            UFSD_MOUNT_CFG *m;

            if (cfg->nmounts >= UFSD_CFG_MAX_MOUNTS) {
                wtof("UFSD101W Parmlib: too many MOUNT statements "
                     "(max %u)", (unsigned)UFSD_CFG_MAX_MOUNTS);
                continue;
            }

            m = &cfg->mounts[cfg->nmounts];
            memset(m, 0, sizeof(*m));
            m->mode = UFSD_MOUNT_RO;  /* default */

            if (!parse_param(p, "DSN", val, sizeof(val))) {
                wtof("UFSD102W Parmlib: MOUNT missing DSN()");
                continue;
            }
            strncpy(m->dsname, val, 44);

            if (!parse_param(p, "PATH", val, sizeof(val))) {
                wtof("UFSD103W Parmlib: MOUNT missing PATH()");
                continue;
            }
            strncpy(m->path, val, 127);

            if (parse_param(p, "MODE", val, sizeof(val))) {
                if (val[0] == 'R' && val[1] == 'W')
                    m->mode = UFSD_MOUNT_RW;
            }

            if (parse_param(p, "OWNER", val, sizeof(val)))
                strncpy(m->owner, val, 8);

            cfg->nmounts++;
            continue;
        }

        wtof("UFSD104W Parmlib: unrecognized statement: %.40s", p);
    }

    fclose(fp);

    if (cfg->root_dsname[0] == '\0') {
        wtof("UFSD105W Parmlib: ROOT statement missing");
        return 8;
    }

    return 0;
}

/* ============================================================
** ufsd_cfg_dump
**
** WTO summary of parsed configuration.
** ============================================================ */
void
ufsd_cfg_dump(const UFSD_CONFIG *cfg)
{
    unsigned i;

    if (!cfg) return;

    wtof("UFSD110I Config: ROOT DSN=%s SIZE=%u BLKSIZE=%u",
         cfg->root_dsname, cfg->root_size, cfg->root_blksize);

    for (i = 0; i < cfg->nmounts; i++) {
        const UFSD_MOUNT_CFG *m = &cfg->mounts[i];

        if (m->owner[0])
            wtof("UFSD111I Config: MOUNT DSN=%s PATH=%s %s OWNER=%s",
                 m->dsname, m->path,
                 m->mode == UFSD_MOUNT_RW ? "RW" : "RO",
                 m->owner);
        else
            wtof("UFSD111I Config: MOUNT DSN=%s PATH=%s %s",
                 m->dsname, m->path,
                 m->mode == UFSD_MOUNT_RW ? "RW" : "RO");
    }
}
