/* UFSFMT.C - UFS370 Disk Format Utility
**
** Batch utility that formats a BDAM dataset as a UFS370 filesystem so a
** root disk can be created on MVS itself, with no host toolchain and no
** HTTPD/mvsMF/ufsd-utils chain in the way.
**
**   //FORMAT   EXEC PGM=UFSFMT
**   //STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
**   //DISKFILE DD  DISP=OLD,DSN=your.ufs.dataset
**   //SYSPRINT DD  SYSOUT=*
**   //SYSTERM  DD  SYSOUT=*
**   //SYSIN    DD  *
**      BLKSIZE  4096
**   /*
**
** Derived from Michael Rayborn's format.c in ufs370-tools, restructured
** and with format_root() rewritten so that no ufs370 library code is
** pulled in -- UFSD implements its own disk I/O by design.  The output
** is byte-compatible with ufsd-utils (pkg/ufs/image.go), which is the
** reference implementation for the on-disk layout.
**
** Interface is SYSIN only; PARM= is not supported, so there is exactly
** one place a parameter can come from and no precedence rules.
**
** Phases, in the order they run.  The boot block is written last on
** purpose: it is the only thing that identifies the dataset as a
** filesystem, so until every other block is down, an interrupted
** format leaves a dataset nobody will mount.
**
**   probe     read sector 0; refuse to wipe a filesystem without FORCE
**   fill      zero the primary extent, counting blocks as it goes
**   format    ilist, free block chain
**   root      root inode, root directory block
**   super     superblock
**   boot      boot block -- the commit point
**   report    what was built, and the MOUNT statement for it
**
** Message numbers
**   01-19   startup, parameters, reformat protection
**   20-39   initialize (fill)
**   40-59   format
**   60-79   root directory
**   80-99   report
**
**   01E BLKSIZE invalid              41E cannot allocate BDAM DCB
**   02E INODES invalid               42E cannot open DD for UPDATE
**   03E DDNAME invalid               43E cannot allocate buffer
**   04E OWNER/GROUP invalid          44E boot block write failed
**   05E/W already a UFS filesystem   45E inode block write failed
**   06I   volume size                46E chain block write failed
**   07I   created                    47W free block count mismatch
**   08I specify FORCE                48E geometry rejected
**   09W PARM= ignored                49E superblock write failed
**   10I banner                       51I index blocks formatted
**   11E unknown keyword              52I data blocks formatted
**   12E duplicate keyword            59I format phase rc
**   13E missing value                60I root phase start
**   14W no SYSIN, defaults apply     61E inode block read failed
**   15E parameter errors             62E root inode write failed
**   16I effective parameters         63E root directory write failed
**   19I parse rc                     65I root inode/block
**   20I initialize start             71I root directory created
**   21E cannot allocate BSAM DCB     79I root phase rc
**   22E cannot open DD for output    80I-86I format summary
**   23E cannot allocate buffer       90I-91I parmlib suggestion
**   24E write error                  99I ending rc
**   25E DISP=SHR, need DISP=OLD
**   26I blocks initialized
**   27E dataset too small
**   39I initialize rc
*/

#include "ufsd.h"
#include "ufsfmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <time64.h>
#include <osio.h>
#include <osdcb.h>
#include <osjfcb.h>

/* Build stamp, same source as the server banner (ufsd.c): mbt generates
** buildstamp.h at every build, so MBT_VERSION is the project version and
** nothing here has to be bumped by hand at release time.  The literal that
** stood here was not in [release] version_files and so would have shipped
** a released UFSFMT still announcing itself as "-dev". */
#include <buildstamp.h>

/* Boot block (sector 0).  Header is 8 bytes, the extension that
** follows carries the only 64-bit timestamps the format has.
** Offsets are written explicitly rather than through a struct: the
** extension mixes 8-byte and 1-byte fields and its alignment is not
** something worth trusting a compiler with. */
#define UFSFMT_DISK_TYPE_UFS   2U
#define UFSFMT_BOOT_V1         1U
#define UFSFMT_BOOT_CREATE  0x08U
#define UFSFMT_BOOT_UPDATE  0x10U
#define UFSFMT_BOOT_VERSION 0x18U

/* Root directory permissions: drwxr-xr-x, as ufsd-utils writes them. */
#define UFSFMT_ROOT_MODE     0755

/* Infinite-loop backstop for the fill phase.  Far above any real
** primary extent (4 GB at BLKSIZE=4096); reaching it means the
** end-of-extent condition never arrived and something is wrong. */
#define UFSFMT_BLOCK_LIMIT   1048576U

/* The on-disk structures are shared with the server (include/ufsd.h).
** If any of them ever changes size, this build stops rather than
** silently writing a filesystem UFSD cannot mount. */
typedef char ufsfmt_chk_sb[(sizeof(UFSD_SB) == 512) ? 1 : -1];
typedef char ufsfmt_chk_di[(sizeof(UFSD_DINODE) == 128) ? 1 : -1];
typedef char ufsfmt_chk_de[(sizeof(UFSD_DIRENT) == 64) ? 1 : -1];

/* ============================================================
** Parameters
** ============================================================ */

typedef struct ufsfmt_parms UFSFMT_PARMS;
struct ufsfmt_parms {
    char     ddname[9];
    char     owner[9];
    char     group[9];
    unsigned blksize;
    double   inode_pct;
    int      force;
    int      quiet;
    int      verbose;
    int      help;
};

/* Keyword identity, doubling as the duplicate-detection bit. */
#define KW_NONE     0x0000U
#define KW_BLKSIZE  0x0001U
#define KW_DDNAME   0x0002U
#define KW_INODES   0x0004U
#define KW_OWNER    0x0008U
#define KW_GROUP    0x0010U
#define KW_FORCE    0x0020U
#define KW_QUIET    0x0040U
#define KW_VERBOSE  0x0080U
#define KW_HELP     0x0100U

/* The keywords that stand alone; everything else is followed by a value. */
#define KW_FLAGS    (KW_FORCE | KW_QUIET | KW_VERBOSE | KW_HELP)

/* SYSIN control statements are read as card images: columns 73-80 are
** a sequence field by MVS convention and are never parameter text. */
#define UFSFMT_CARD_COLS  72

/* Existing filesystem, as found by the probe phase. */
typedef struct ufsfmt_found UFSFMT_FOUND;
struct ufsfmt_found {
    int       is_ufs;
    unsigned  blksize;
    unsigned  volume_size;
    mtime64_t create_time;
    int       have_time;
};

static const char *help_text[] = {
"UFSFMT -- UFS370 disk format utility",
" ",
"Formats a BDAM dataset as a UFS370 filesystem with an empty root",
"directory, ready to be mounted by the UFSD started task.",
" ",
"Control statements are read from SYSIN.  PARM= is not supported.",
"Each keyword may appear at most once; a repeated keyword is an",
"error, not a silent override.  Keywords must be written in full.",
"Comments are delimited by slash-star and star-slash, and must not",
"start in column 1 of an instream SYSIN.  Columns 73-80 are ignored.",
" ",
"Keyword   Value                    Default   Meaning",
"BLKSIZE   512-8192, multiple 512   4096      Block size in bytes",
"DDNAME    DD name                  DISKFILE  DD of dataset to format",
"INODES    1.0-50.0                 10.0      Percent of blocks for inodes",
"OWNER     1-8 characters           (none)    Root directory owner",
"GROUP     1-8 characters           (none)    Root directory group",
"FORCE     --                       off       Overwrite an existing UFS",
"QUIET     --                       off       Suppress messages and report",
"VERBOSE   --                       off       Extra per-phase messages",
"HELP      --                       --        Print this text",
" ",
"DD statements",
"SYSIN     Control statements (may be omitted or DUMMY)",
"DISKFILE  Dataset to format, DISP=OLD (name overridable via DDNAME)",
"SYSPRINT  Report",
"SYSTERM   Messages",
" ",
"Only the primary extent of the dataset is formatted.  Do not give a",
"secondary quantity: it would never be used, and the space would be",
"invisible to the filesystem.  MVS rounds the allocation up to a track",
"boundary, so the disk may be slightly larger than requested; UFSFMT",
"formats what the primary extent actually provides and reports the",
"real block count as message UFSFMT26I.",
" ",
"An existing UFS370 filesystem is not overwritten unless FORCE is",
"given.  The dataset must be allocated DISP=OLD.",
" ",
"Without OWNER and GROUP the root directory is left unowned.  The",
"fields are metadata: who may write to the filesystem is decided by",
"OWNER() on the UFSD parmlib MOUNT statement, not by them.  Give",
"OWNER here when you want the disk to carry the name of the userid",
"it is being handed to.",
" ",
"Example",
"//FORMAT   EXEC PGM=UFSFMT",
"//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB",
"//DISKFILE DD  DISP=OLD,DSN=MIKEG1.UFSHOME",
"//SYSPRINT DD  SYSOUT=*",
"//SYSTERM  DD  SYSOUT=*",
"//SYSIN    DD  *",
"   BLKSIZE  4096",
"   INODES   10.0",
"   OWNER    HERC01",
"   GROUP    ADMIN",
NULL
};

/* Forward declarations */
static int  parse_sysin(UFSFMT_PARMS *p);
static void probe_disk(const UFSFMT_PARMS *p, UFSFMT_FOUND *found);
static int  fill_disk(const UFSFMT_PARMS *p, char *dsn, unsigned *blocks);
static int  format_disk(DCB *dcb, const UFSFMT_GEOM *g, char *buf);
static int  format_root(DCB *dcb, const UFSFMT_GEOM *g, mtime64_t now,
                        char *buf, const UFSFMT_PARMS *p);
static int  write_super(DCB *dcb, const UFSFMT_GEOM *g, char *buf);
static int  write_boot(DCB *dcb, const UFSFMT_GEOM *g, mtime64_t now,
                       char *buf);
static void report(const UFSFMT_PARMS *p, const UFSFMT_GEOM *g,
                   const char *dsn);

static DCB *disk_open(const char *ddname);
static void disk_close(DCB *dcb);
static int  block_read(DCB *dcb, char *buf, unsigned blksize, unsigned sector);
static int  block_write(DCB *dcb, char *buf, unsigned blksize, unsigned sector);

/* ============================================================
** msg / err
**
** Informational output goes to SYSPRINT and is suppressed by QUIET.
** Errors and warnings go to SYSTERM and are never suppressed: a
** format that failed must say so even when asked to be quiet.
** ============================================================ */
static int s_quiet = 0;

static void
msg(const char *fmt, ...)
{
    va_list ap;

    if (s_quiet) return;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

static void
err(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* ============================================================
** fmt_mb
**
** Blocks to megabytes, for the "(1.00 MB)" parts of the messages.
** ============================================================ */
static double
fmt_mb(unsigned blocks, unsigned blksize)
{
    return ((double)blocks * (double)blksize) / 1048576.0;
}

/* ============================================================
** main
** ============================================================ */
int
main(int argc, char **argv)
{
    UFSFMT_PARMS p;
    UFSFMT_GEOM  g;
    UFSFMT_FOUND found;
    char         dsn[45];
    char        *buf;
    DCB         *dcb;
    mtime64_t    now;
    unsigned     blocks;
    int          rc;

    (void)argv;

    /* No owner and no group unless OWNER/GROUP say otherwise (#62):
    ** a formatter has no way of knowing who a filesystem belongs to,
    ** and a name invented here would be indistinguishable from one
    ** the operator chose.  The memset leaves both empty, which UFSD
    ** reads as unowned -- and access is decided by OWNER() on the
    ** MOUNT statement either way, never by this field. */
    memset(&p, 0, sizeof(p));
    strcpy(p.ddname, "DISKFILE");
    p.blksize   = 4096U;
    p.inode_pct = 10.0;

    /* PARM= is deliberately unsupported.  Say so rather than letting a
    ** job that was migrated from PGM=FORMAT format with the defaults
    ** and look like it honoured its parameters. */
    if (argc > 1)
        err("UFSFMT09W PARM= IS NOT SUPPORTED -- "
            "CONTROL STATEMENTS ARE READ FROM SYSIN\n");

    /* SYSIN is read before the banner because QUIET is only knowable
    ** afterwards, and QUIET has to mean it. */
    rc = parse_sysin(&p);
    s_quiet = p.quiet;

    msg("UFSFMT10I UFSFMT %s -- UFS370 disk format utility\n", MBT_VERSION);

    if (p.help) {
        int i;
        for (i = 0; help_text[i]; i++)
            printf("%s\n", help_text[i]);
        return 4;
    }

    if (rc) {
        err("UFSFMT15E PARAMETER ERRORS -- DATASET NOT FORMATTED\n");
        return 8;
    }

    if (p.verbose) {
        msg("UFSFMT16I DDNAME=%s BLKSIZE=%u INODES=%.1f "
            "OWNER=%s GROUP=%s%s\n",
            p.ddname, p.blksize, p.inode_pct,
            ufsfmt_owner_text(p.owner), ufsfmt_owner_text(p.group),
            p.force ? " FORCE" : "");
        msg("UFSFMT19I PARAMETERS ACCEPTED\n");
    }

    /* --- probe: refuse to wipe a filesystem by accident ----------- */
    memset(&found, 0, sizeof(found));
    probe_disk(&p, &found);

    if (found.is_ufs) {
        /* Either way the filesystem that is there gets described.
        ** FORCE is the one path in this program that destroys data
        ** somebody may still want, so what it destroyed belongs in
        ** the job log whether or not the job went on to succeed. */
        if (p.force)
            err("UFSFMT05W OVERWRITING AN EXISTING UFS370 FILESYSTEM "
                "(FORCE SPECIFIED)\n");
        else
            err("UFSFMT05E DATASET ALREADY CONTAINS "
                "A UFS370 FILESYSTEM\n");

        if (found.volume_size)
            err("UFSFMT06I   Volume size . . . %u blocks "
                "(%.2f MB), block size %u\n",
                found.volume_size,
                fmt_mb(found.volume_size, found.blksize),
                found.blksize);

        if (found.have_time) {
            struct tm *tmv = mlocaltime64(&found.create_time);
            if (tmv)
                err("UFSFMT07I   Created . . . . . "
                    "%04d-%02d-%02d %02d:%02d:%02d\n",
                    tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                    tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
        }

        if (!p.force) {
            err("UFSFMT08I SPECIFY FORCE TO OVERWRITE\n");
            return 8;
        }
    }

    /* --- fill: zero the primary extent and learn its size --------- */
    if (p.verbose)
        msg("UFSFMT20I INITIALIZING DD %s\n", p.ddname);

    dsn[0] = '\0';
    blocks = 0;
    rc = fill_disk(&p, dsn, &blocks);
    if (p.verbose)
        msg("UFSFMT39I INITIALIZE ENDED WITH RC=%d\n", rc);
    if (rc) return rc;

    msg("UFSFMT26I Initialized %u blocks (%.2f MB)\n",
        blocks, fmt_mb(blocks, p.blksize));

    /* --- geometry ------------------------------------------------- */
    rc = ufsfmt_geometry(&g, blocks, p.blksize, p.inode_pct);
    if (rc == UFSFMT_GEOM_TOOSMALL) {
        err("UFSFMT27E DATASET TOO SMALL: %u BLOCKS OF %u BYTES "
                    "CANNOT HOLD A FILESYSTEM\n", blocks, p.blksize);
        return 8;
    }
    if (rc != UFSFMT_GEOM_OK) {
        err("UFSFMT48E GEOMETRY REJECTED (RC=%d)\n", rc);
        return 8;
    }

    /* --- format + root -------------------------------------------- */
    if (p.verbose)
        msg("UFSFMT40I FORMATTING DD %s AS A FILESYSTEM\n", p.ddname);

    dcb = disk_open(p.ddname);
    if (!dcb) return 8;

    buf = (char *)calloc(1, p.blksize);
    if (!buf) {
        err("UFSFMT43E CANNOT ALLOCATE %u BYTE BUFFER\n", p.blksize);
        disk_close(dcb);
        return 8;
    }

    mtime64(&now);

    rc = format_disk(dcb, &g, buf);
    if (p.verbose)
        msg("UFSFMT59I FORMAT ENDED WITH RC=%d\n", rc);

    if (!rc) {
        if (p.verbose)
            msg("UFSFMT60I CREATING ROOT DIRECTORY\n");
        rc = format_root(dcb, &g, now, buf, &p);
        if (p.verbose)
            msg("UFSFMT79I ROOT DIRECTORY ENDED WITH RC=%d\n", rc);
    }

    if (!rc)
        rc = write_super(dcb, &g, buf);

    /* The boot block is what makes the dataset a filesystem, so it is
    ** written only once everything it would vouch for is on disk. */
    if (!rc)
        rc = write_boot(dcb, &g, now, buf);

    free(buf);
    disk_close(dcb);

    if (rc) return rc;

    report(&p, &g, dsn);

    if (p.verbose)
        msg("UFSFMT99I UFSFMT ENDING WITH RC=0\n");

    return 0;
}

/* ============================================================
** strip_comments
**
** Remove slash-star comments from one line, honouring a comment that
** spans lines through *in_comment.  Same convention as the UFSD
** parmlib member (src/ufsd#cfg.c).
** ============================================================ */
static void
strip_comments(char *line, int *in_comment)
{
    char *src;
    char *dst;

    src = line;
    dst = line;

    while (*src) {
        if (*in_comment) {
            if (src[0] == '*' && src[1] == '/') {
                *in_comment = 0;
                src += 2;
            } else {
                src++;
            }
        } else if (src[0] == '/' && src[1] == '*') {
            *in_comment = 1;
            src += 2;
            *dst++ = ' ';       /* a comment separates tokens */
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ============================================================
** kw_lookup
**
** Map a keyword to its bit.  Full spelling only -- an abbreviation
** that is unique today stops being unique when a keyword is added.
** ============================================================ */
static unsigned
kw_lookup(const char *tok)
{
    if (strcmp(tok, "BLKSIZE") == 0) return KW_BLKSIZE;
    if (strcmp(tok, "DDNAME")  == 0) return KW_DDNAME;
    if (strcmp(tok, "INODES")  == 0) return KW_INODES;
    if (strcmp(tok, "OWNER")   == 0) return KW_OWNER;
    if (strcmp(tok, "GROUP")   == 0) return KW_GROUP;
    if (strcmp(tok, "FORCE")   == 0) return KW_FORCE;
    if (strcmp(tok, "QUIET")   == 0) return KW_QUIET;
    if (strcmp(tok, "VERBOSE") == 0) return KW_VERBOSE;
    if (strcmp(tok, "HELP")    == 0) return KW_HELP;
    return KW_NONE;
}

/* ============================================================
** valid_name
**
** MVS name rules for DD names, userids and group names: 1-8
** characters, alphanumeric or one of @ # $, not starting with a
** digit.  Copies the uppercased name to out (9 bytes).
** ============================================================ */
static int
valid_name(const char *tok, char *out)
{
    int len;
    int i;

    len = (int)strlen(tok);
    if (len < 1 || len > 8) return 0;
    if (isdigit((unsigned char)tok[0])) return 0;

    for (i = 0; i < len; i++) {
        if (!isalnum((unsigned char)tok[i]) && !strchr("@#$", tok[i]))
            return 0;
        out[i] = (char)toupper((unsigned char)tok[i]);
    }
    out[len] = '\0';
    return 1;
}

/* ============================================================
** valid_number
**
** Accept digits with at most one decimal point.  atof() would take
** "4O96" as 4 and format a disk nobody asked for.
** ============================================================ */
static int
valid_number(const char *tok, int allow_dot)
{
    int dots;
    int digits;
    int i;

    dots   = 0;
    digits = 0;

    for (i = 0; tok[i]; i++) {
        if (tok[i] == '.') {
            if (!allow_dot || ++dots > 1) return 0;
        } else if (isdigit((unsigned char)tok[i])) {
            digits++;
        } else {
            return 0;
        }
    }
    return digits > 0;
}

/* ============================================================
** apply_value
**
** Apply the value token of a keyword that takes one.
** Returns 0 on success, 8 on a rejected value.
** ============================================================ */
static int
apply_value(UFSFMT_PARMS *p, unsigned kw, const char *tok)
{
    switch (kw) {
    case KW_BLKSIZE:
        if (!valid_number(tok, 0)) {
            err("UFSFMT01E BLKSIZE VALUE \"%s\" IS NOT NUMERIC\n", tok);
            return 8;
        }
        p->blksize = (unsigned)strtoul(tok, NULL, 10);
        if (p->blksize < UFSFMT_MIN_BLKSIZE
            || p->blksize > UFSFMT_MAX_BLKSIZE
            || (p->blksize % UFSFMT_MIN_BLKSIZE) != 0U) {
            err("UFSFMT01E BLKSIZE %u MUST BE 512 TO 8192 "
                        "AND A MULTIPLE OF 512\n", p->blksize);
            return 8;
        }
        return 0;

    case KW_INODES:
        if (!valid_number(tok, 1)) {
            err("UFSFMT02E INODES VALUE \"%s\" IS NOT NUMERIC\n", tok);
            return 8;
        }
        p->inode_pct = atof(tok);
        if (p->inode_pct < UFSFMT_MIN_INODEPCT
            || p->inode_pct > UFSFMT_MAX_INODEPCT) {
            err("UFSFMT02E INODES %.1f MUST BE 1.0 TO 50.0\n",
                p->inode_pct);
            return 8;
        }
        return 0;

    case KW_DDNAME:
        if (!valid_name(tok, p->ddname)) {
            err("UFSFMT03E DDNAME \"%s\" IS NOT A VALID DD NAME\n",
                tok);
            return 8;
        }
        return 0;

    case KW_OWNER:
        if (!valid_name(tok, p->owner)) {
            err("UFSFMT04E OWNER \"%s\" IS NOT A VALID NAME\n", tok);
            return 8;
        }
        return 0;

    case KW_GROUP:
        if (!valid_name(tok, p->group)) {
            err("UFSFMT04E GROUP \"%s\" IS NOT A VALID NAME\n", tok);
            return 8;
        }
        return 0;

    default:
        return 8;
    }
}

/* ============================================================
** parse_sysin
**
** Read control statements from SYSIN (stdin, mapped by libc370) as a
** flat stream of tokens, so a keyword and its value may sit on one
** line or on two.  A missing or DUMMY SYSIN is legal: every default
** applies and the report shows what they were.
**
** Returns 0 if every statement was accepted, 8 otherwise.
** ============================================================ */
static int
parse_sysin(UFSFMT_PARMS *p)
{
    char     line[256];
    char    *tok;
    unsigned seen;
    unsigned pending;
    unsigned kw;
    int      discard;
    int      in_comment;
    int      rc;

    seen       = 0;
    pending    = KW_NONE;
    discard    = 0;
    in_comment = 0;
    rc         = 0;

    if (!stdin) {
        err("UFSFMT14W NO SYSIN -- ALL DEFAULTS APPLY\n");
        return 0;
    }

    while (fgets(line, (int)sizeof(line), stdin)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* Card images: columns 73-80 are the sequence field. */
        if (strlen(line) > UFSFMT_CARD_COLS)
            line[UFSFMT_CARD_COLS] = '\0';

        strip_comments(line, &in_comment);

        for (tok = strtok(line, " \t"); tok; tok = strtok(NULL, " \t")) {
            if (pending != KW_NONE) {
                if (apply_value(p, pending, tok))
                    rc = 8;
                pending = KW_NONE;
                continue;
            }

            /* Value of a keyword that was already rejected.  Swallow
            ** it: reporting "512" as an unknown keyword on the line
            ** after "BLKSIZE specified twice" buries the real error in
            ** a second one that is not a mistake the user made. */
            if (discard) {
                discard = 0;
                continue;
            }

            /* Keywords are matched uppercase; lower case in SYSIN is
            ** accepted because a card punched from a PC editor often
            ** is not folded. */
            {
                char up[32];
                unsigned i;
                unsigned len = (unsigned)strlen(tok);

                if (len >= sizeof(up)) len = sizeof(up) - 1U;
                for (i = 0; i < len; i++)
                    up[i] = (char)toupper((unsigned char)tok[i]);
                up[len] = '\0';

                kw = kw_lookup(up);
            }

            if (kw == KW_NONE) {
                err("UFSFMT11E UNKNOWN KEYWORD \"%s\"\n", tok);
                rc = 8;
                continue;
            }
            if (seen & kw) {
                err("UFSFMT12E KEYWORD \"%s\" SPECIFIED MORE "
                    "THAN ONCE\n", tok);
                rc = 8;
                if (!(kw & KW_FLAGS))
                    discard = 1;
                continue;
            }
            seen |= kw;

            switch (kw) {
            case KW_FORCE:   p->force   = 1; break;
            case KW_QUIET:   p->quiet   = 1; break;
            case KW_VERBOSE: p->verbose = 1; break;
            case KW_HELP:    p->help    = 1; break;
            default:         pending    = kw; break;
            }
        }
    }

    if (pending != KW_NONE) {
        err("UFSFMT13E KEYWORD AT END OF SYSIN HAS NO VALUE\n");
        rc = 8;
    }

    return rc;
}

/* ============================================================
** disk_open
**
** Open the dataset for BDAM UPDATE and install a SYNAD stub so an
** I/O error surfaces through oscheck() instead of IEC020I on the
** console.  Same pattern the server uses (src/ufsd#ini.c).
** ============================================================ */
static DCB *
disk_open(const char *ddname)
{
    DCB *dcb;

    dcb = osddcb(ddname);
    if (!dcb) {
        err("UFSFMT41E CANNOT ALLOCATE BDAM DCB FOR DD %s\n", ddname);
        return NULL;
    }

    {
        void *synad;
        __asm__("B\tUFSFSYE\n\t"
            "DS\t0H\n"
            "UFSFSYD\tDS\t0H\n\t"
            "BR\t14\n"
            "UFSFSYE\tDS\t0H\n\t"
            "LA\t%0,UFSFSYD" : "=r"(synad));
        dcb->dcbsynad = synad;
    }

    if (osdopen(dcb, 0)) {
        err("UFSFMT42E CANNOT OPEN DD %s FOR UPDATE\n", ddname);
        free(dcb);
        return NULL;
    }

    return dcb;
}

/* ============================================================
** disk_close
** ============================================================ */
static void
disk_close(DCB *dcb)
{
    if (dcb) osdclose(dcb, 1);      /* 1 = free DCB storage */
}

/* ============================================================
** block_read / block_write
**
** oscheck() carries the completion status; the return value of
** osdread/osdwrite alone says only that the request was accepted.
** ============================================================ */
static int
block_read(DCB *dcb, char *buf, unsigned blksize, unsigned sector)
{
    DECB decb;

    memset(&decb, 0, sizeof(decb));
    osdread(&decb, dcb, buf, (int)blksize, sector);
    return oscheck(&decb) ? 8 : 0;
}

static int
block_write(DCB *dcb, char *buf, unsigned blksize, unsigned sector)
{
    DECB decb;

    memset(&decb, 0, sizeof(decb));
    osdwrite(&decb, dcb, buf, (int)blksize, sector);
    return oscheck(&decb) ? 8 : 0;
}

/* ============================================================
** probe_disk
**
** Look for an existing filesystem before anything is overwritten.
** Best effort by design: on a freshly allocated dataset the BDAM
** open or the read of sector 0 may well fail, and that is exactly
** the case with nothing to protect.  Any failure therefore means
** "no filesystem found", never "stop".
**
** The authoritative DISP=SHR check and the dataset name come from
** the fill phase, which has to open the dataset anyway.
** ============================================================ */
static void
probe_disk(const UFSFMT_PARMS *p, UFSFMT_FOUND *found)
{
    DCB            *dcb;
    char           *buf;
    unsigned short *hdr;
    unsigned        blksize;

    dcb = osddcb(p->ddname);
    if (!dcb) return;

    {
        void *synad;
        __asm__("B\tUFSFPRE\n\t"
            "DS\t0H\n"
            "UFSFPRD\tDS\t0H\n\t"
            "BR\t14\n"
            "UFSFPRE\tDS\t0H\n\t"
            "LA\t%0,UFSFPRD" : "=r"(synad));
        dcb->dcbsynad = synad;
    }

    if (osdopen(dcb, 0)) {
        free(dcb);
        return;
    }

    /* osdopen took the block size from the JFCB.  A dataset without
    ** one cannot be read block-wise, so there is nothing to probe. */
    blksize = (unsigned)dcb->dcbblksi;
    if (blksize < UFSFMT_MIN_BLKSIZE || blksize > UFSFMT_MAX_BLKSIZE) {
        osdclose(dcb, 1);
        return;
    }

    buf = (char *)calloc(1, blksize);
    if (!buf) {
        osdclose(dcb, 1);
        return;
    }

    if (block_read(dcb, buf, blksize, 0)) {
        free(buf);
        osdclose(dcb, 1);
        return;                     /* never written -- not a filesystem */
    }

    hdr = (unsigned short *)buf;
    if (hdr[0] != (unsigned short)UFSFMT_DISK_TYPE_UFS
        || (unsigned)(hdr[0] + hdr[1]) != 0xFFFFU) {
        free(buf);
        osdclose(dcb, 1);
        return;
    }

    found->is_ufs  = 1;
    found->blksize = hdr[2] ? (unsigned)hdr[2] : blksize;

    if ((unsigned char)buf[UFSFMT_BOOT_VERSION] >= UFSFMT_BOOT_V1) {
        memcpy(&found->create_time, buf + UFSFMT_BOOT_CREATE,
               sizeof(found->create_time));
        found->have_time = 1;
    }

    /* Volume size lives in the superblock, one sector further in. */
    if (block_read(dcb, buf, blksize, 1) == 0) {
        UFSD_SB *sb = (UFSD_SB *)buf;
        if (sb->volume_size)
            found->volume_size = sb->volume_size;
    }

    free(buf);
    osdclose(dcb, 1);
    return;
}

/* ============================================================
** fill_disk
**
** Write zero blocks into the primary extent until the extent is
** exhausted, counting as it goes.  That count is the volume size:
** the filesystem is exactly as large as the primary extent, which is
** why a secondary quantity must not be allocated -- its blocks would
** never be formatted and never be reachable.
**
** Termination comes from CHECK: when the primary extent is full the
** end-of-volume routine abends D37 (or B37 with a secondary defined),
** and oscheck() catches it through its ESTAE.  Only a block whose
** CHECK succeeded is counted, so the reported size can never exceed
** what is really on disk.
**
** Returns 0 on success, 8 on error.  Fills dsn from the JFCB.
** ============================================================ */
static int
fill_disk(const UFSFMT_PARMS *p, char *dsn, unsigned *blocks)
{
    DCB     *dcb;
    char    *buf;
    DECB     decb;
    JFCB     jfcb;
    unsigned count;
    int      i;

    *blocks = 0;

    dcb = osbdcb(p->ddname, NULL);
    if (!dcb) {
        err("UFSFMT21E CANNOT ALLOCATE BSAM DCB FOR DD %s\n",
            p->ddname);
        return 8;
    }

    buf = (char *)calloc(1, p->blksize);
    if (!buf) {
        err("UFSFMT23E CANNOT ALLOCATE %u BYTE BUFFER\n", p->blksize);
        free(dcb);
        return 8;
    }

    /* Open for input first: RDJFCB needs an open DCB, and the
    ** disposition has to be known before the first byte is written. */
    if (osbopen(dcb, 0, "r")) {
        err("UFSFMT22E CANNOT OPEN DD %s FOR READ\n", p->ddname);
        free(buf);
        free(dcb);
        return 8;
    }

    memset(&jfcb, 0, sizeof(jfcb));
    __rdjfcb(dcb, &jfcb);

    for (i = 0; i < 44 && jfcb.jfcbdsnm[i] > ' '; i++)
        dsn[i] = jfcb.jfcbdsnm[i];
    dsn[i] = '\0';

    if (jfcb.jfcbind2 & JFCSHARE) {
        /* A DISP=SHR allocation means someone else may be reading the
        ** dataset while it is being wiped, and UFSD marks such a disk
        ** read-only anyway. */
        err("UFSFMT25E DD %s IS ALLOCATED DISP=SHR -- "
                    "DISP=OLD IS REQUIRED\n", p->ddname);
        osbclose(dcb, NULL, 1, 0);
        free(buf);
        return 8;
    }

    osbclose(dcb, NULL, 0, 0);      /* keep the DCB, reopen for output */

    dcb->dcbblksi = (unsigned short)p->blksize;
    dcb->dcblrecl = dcb->dcbblksi;

    /* Plain BSAM output, not load mode.  MACRF=(WL) is the create-BDAM
    ** interface and wants the SD/SZ form of WRITE; libc370's oswrite
    ** issues SF, and the mismatch leaves the DECB unposted, so the
    ** first CHECK waits for a completion that never arrives -- the job
    ** sits in the format step burning no CPU until it is cancelled.
    ** Sequential output writes the same blocks, and BDAM addresses
    ** them afterwards by relative block number regardless of how they
    ** got there (the same way UFSD reads an uploaded image). */
    if (osbopen(dcb, 0, "w")) {
        err("UFSFMT22E CANNOT OPEN DD %s FOR OUTPUT\n", p->ddname);
        free(buf);
        free(dcb);
        return 8;
    }

    count = 0;
    for (;;) {
        memset(&decb, 0, sizeof(decb));

        oswrite(&decb, dcb, buf, (int)p->blksize);

        /* CHECK carries the verdict, and it is the only thing that
        ** does: what BSAM leaves in R15 after a sequential WRITE is
        ** not a status, and was measured to be an arbitrary negative
        ** value on writes that completed perfectly well.  Believing it
        ** would end the loop early and silently build a filesystem
        ** smaller than the extent it was given. */
        if (oscheck(&decb)) break;      /* D37: primary extent full */

        count++;
        if (count >= UFSFMT_BLOCK_LIMIT) {
            err("UFSFMT24E DD %s DID NOT REACH END OF EXTENT "
                        "AFTER %u BLOCKS\n", p->ddname, count);
            osbclose(dcb, NULL, 1, 0);
            free(buf);
            return 8;
        }
    }

    osbclose(dcb, NULL, 1, 0);      /* close and free the DCB */
    free(buf);

    if (count == 0) {
        err("UFSFMT24E NO BLOCKS COULD BE WRITTEN TO DD %s\n",
            p->ddname);
        return 8;
    }

    *blocks = count;
    return 0;
}

/* ============================================================
** format_disk
**
** Boot block, inode list and free block chain.  The superblock is
** not written here -- see write_super().
** ============================================================ */
static int
format_disk(DCB *dcb, const UFSFMT_GEOM *g, char *buf)
{
    /* One V7 free list chain block: a count followed by block
    ** numbers, which is how ufsd_sb_alloc_block() reads it back
    ** (chain[0] = count, chain[1..] = blocks, src/ufsd#sbl.c). */
    struct chain {
        unsigned nfreeblock;
        unsigned freeblock[UFSFMT_MAX_FREEBLOCK];
    } chain;

    unsigned        sector;
    unsigned        block;
    unsigned        chain_sector;
    unsigned        chained;
    unsigned        i;

    /* --- inode blocks --------------------------------------------- */
    for (i = 0; i < g->inode_blocks; i++) {
        sector = UFSFMT_ILIST_SECTOR + i;

        memset(buf, 0, g->blksize);
        if (i == 0) {
            /* Slots 0 and 1 hold inodes 1 and 2.  Inode 1 is the
            ** BALBLK monument and inode 2 becomes the root; both are
            ** marked occupied by filling them with 0xFF, which is
            ** what an ilist scan reads as "not free". */
            memset(buf, 0xFF, UFSFMT_INODE_SIZE * 2U);
        }

        if (block_write(dcb, buf, g->blksize, sector)) {
            err("UFSFMT45E WRITE OF INODE BLOCK %u FAILED\n", sector);
            return 8;
        }
    }

    /* --- free block chain ----------------------------------------- */
    /* The first up-to-51 data blocks are cached in the superblock.
    ** Everything above that is described by chain blocks, each stored
    ** in the block the cache before it runs out on -- so the first
    ** chain block sits in datablock_start, and each following one in
    ** the lowest block its predecessor listed. */
    chain_sector = g->datablock_start;
    chained      = 0;
    memset(&chain, 0, sizeof(chain));

    for (block = g->chain_start; block < g->total_blocks; block++) {
        chain.freeblock[chain.nfreeblock++] = block;

        if (chain.nfreeblock == UFSFMT_MAX_FREEBLOCK
            || block + 1U == g->total_blocks) {

            memset(buf, 0, g->blksize);
            memcpy(buf, &chain, sizeof(chain));

            if (block_write(dcb, buf, g->blksize, chain_sector)) {
                err("UFSFMT46E WRITE OF FREE CHAIN BLOCK %u "
                            "FAILED\n", chain_sector);
                return 8;
            }

            chained     += chain.nfreeblock;
            chain_sector = chain.freeblock[0];
            memset(&chain, 0, sizeof(chain));
        }
    }

    /* Plausibility only: total_freeblock comes from the geometry, not
    ** from this loop.  A mismatch means the chain does not describe
    ** the volume the superblock claims, which is worth saying even
    ** though the counters themselves are sound. */
    if (g->sb_cache_blocks + chained
        != g->total_blocks - g->datablock_start)
        err("UFSFMT47W FREE BLOCK CHAIN COVERS %u BLOCKS, "
                    "EXPECTED %u\n",
            g->sb_cache_blocks + chained,
            g->total_blocks - g->datablock_start);

    msg("UFSFMT51I Formatted %u index blocks, %u inode slots\n",
        g->inode_blocks, g->inode_slots);
    msg("UFSFMT52I Formatted %u data blocks\n",
        g->total_blocks - g->datablock_start);

    return 0;
}

/* ============================================================
** write_boot
**
** Write the boot block, and with it the only thing that makes the
** dataset a filesystem: UFSD's mount check reads sector 0 and refuses
** anything whose type is not UFS (src/ufsd#ini.c, open_disk).
**
** It goes down last for exactly that reason.  The fill phase has
** already zeroed sector 0, so a format that abends part-way leaves a
** dataset UFSD will not mount and UFSFMT will not treat as an
** existing filesystem -- rerunning it needs no FORCE.  Writing it
** first, as the reference implementations do, would leave a window in
** which the disk announces a filesystem it does not yet have.
** ============================================================ */
static int
write_boot(DCB *dcb, const UFSFMT_GEOM *g, mtime64_t now, char *buf)
{
    unsigned short *hdr;

    memset(buf, 0, g->blksize);

    hdr = (unsigned short *)buf;
    hdr[0] = (unsigned short)UFSFMT_DISK_TYPE_UFS;
    hdr[1] = (unsigned short)~(unsigned short)UFSFMT_DISK_TYPE_UFS;
    hdr[2] = (unsigned short)g->blksize;
    hdr[3] = 0;

    /* Boot extension: the format's real timestamps.  The superblock
    ** has no room for a 64-bit time, which is why its own time fields
    ** stay zero. */
    memcpy(buf + UFSFMT_BOOT_CREATE, &now, sizeof(now));
    memcpy(buf + UFSFMT_BOOT_UPDATE, &now, sizeof(now));
    buf[UFSFMT_BOOT_VERSION] = (char)UFSFMT_BOOT_V1;

    if (block_write(dcb, buf, g->blksize, 0)) {
        err("UFSFMT44E WRITE OF BOOT BLOCK FAILED\n");
        return 8;
    }

    return 0;
}

/* ============================================================
** format_root
**
** Write the root inode and its directory block.  No allocator is
** involved: which inode and which data block the root gets is fixed
** by the geometry, so this is three buffers and two writes.
** ============================================================ */
static int
format_root(DCB *dcb, const UFSFMT_GEOM *g, mtime64_t now,
            char *buf, const UFSFMT_PARMS *p)
{
    UFSD_DINODE *dino;
    UFSD_DIRENT *de;

    /* Read the first inode block back rather than rebuilding it: the
    ** slot has to be cleared from 0xFF, and reading confirms the
    ** block that was just written is really there. */
    if (block_read(dcb, buf, g->blksize, UFSFMT_ILIST_SECTOR)) {
        err("UFSFMT61E READ OF INODE BLOCK %u FAILED\n",
            UFSFMT_ILIST_SECTOR);
        return 8;
    }

    /* Inode 2 is slot 1 of the first block.  Zero the whole slot: it
    ** was filled with 0xFF as a reserved marker, and anything left
    ** behind would show up as a codepage and as garbage in addr[1..18]. */
    dino = (UFSD_DINODE *)(buf + UFSFMT_INODE_SIZE);
    memset(dino, 0, sizeof(*dino));

    dino->mode     = (unsigned short)(UFSD_IFDIR | UFSFMT_ROOT_MODE);
    dino->nlink    = 2;                             /* "." and ".."   */
    dino->filesize = UFSFMT_DIRENT_SIZE * 2U;
    dino->ctime.v2 = now;
    dino->mtime.v2 = now;
    dino->atime.v2 = now;
    dino->codepage = 0;
    dino->addr[0]  = g->root_block;

    /* owner/group are NUL-padded, not blank-padded (ufs370 convention). */
    strncpy(dino->owner, p->owner, sizeof(dino->owner) - 1U);
    strncpy(dino->group, p->group, sizeof(dino->group) - 1U);

    if (block_write(dcb, buf, g->blksize, UFSFMT_ILIST_SECTOR)) {
        err("UFSFMT62E WRITE OF ROOT INODE FAILED\n");
        return 8;
    }

    /* Directory block: "." and "..", both pointing at the root -- the
    ** root is its own parent.  Names are native EBCDIC; nothing is
    ** translated here because nothing came from outside MVS. */
    memset(buf, 0, g->blksize);
    de = (UFSD_DIRENT *)buf;
    de[0].ino = UFSFMT_ROOT_INO;
    strcpy(de[0].name, ".");
    de[1].ino = UFSFMT_ROOT_INO;
    strcpy(de[1].name, "..");

    if (block_write(dcb, buf, g->blksize, g->root_block)) {
        err("UFSFMT63E WRITE OF ROOT DIRECTORY BLOCK %u FAILED\n",
            g->root_block);
        return 8;
    }

    if (p->verbose)
        msg("UFSFMT65I ROOT INODE %u, DATA BLOCK %u\n",
            UFSFMT_ROOT_INO, g->root_block);

    msg("UFSFMT71I Root directory created, owner=%s, group=%s, mode=0%o\n",
        ufsfmt_owner_text(p->owner), ufsfmt_owner_text(p->group),
        UFSFMT_ROOT_MODE);

    return 0;
}

/* ============================================================
** write_super
**
** Build the superblock from the geometry and write it.  Every count
** here was derived once, in ufsfmt_geometry(), rather than
** accumulated while writing.
**
** update_time and create_time stay zero: the superblock has no room
** for a 64-bit timestamp, so the format's real timestamps live in the
** boot extension.  ufsd-utils writes zeroes here too.
** ============================================================ */
static int
write_super(DCB *dcb, const UFSFMT_GEOM *g, char *buf)
{
    UFSD_SB *sb;
    unsigned i;

    memset(buf, 0, g->blksize);
    sb = (UFSD_SB *)buf;

    sb->datablock_start  = g->datablock_start;
    sb->volume_size      = g->total_blocks;
    sb->lock_freeblock   = 0;
    sb->lock_freeinode   = 0;
    sb->modified         = 0;
    sb->rdonly           = 0;
    sb->update_time      = 0;
    sb->total_freeblock  = g->total_freeblock;
    sb->total_freeinode  = g->total_freeinode;
    sb->create_time      = 0;
    sb->nfreeblock       = g->nfreeblock;
    sb->nfreeinode       = g->nfreeinode;
    sb->inodes_per_block = g->inodes_per_block;
    sb->blksize_shift    = g->blksize_shift;
    sb->ilist_sector     = UFSFMT_ILIST_SECTOR;

    /* Both caches are copied in full.  Entries at or above the live
    ** count are never read by UFSD, and copying them verbatim keeps
    ** the superblock byte-identical to a ufsd-utils image. */
    for (i = 0; i < UFSFMT_MAX_FREEBLOCK; i++)
        sb->freeblock[i] = g->freeblock[i];
    for (i = 0; i < UFSFMT_MAX_FREEINODE; i++)
        sb->freeinode[i] = g->freeinode[i];

    if (block_write(dcb, buf, g->blksize, 1)) {
        err("UFSFMT49E WRITE OF SUPERBLOCK FAILED\n");
        return 8;
    }

    return 0;
}

/* ============================================================
** report
**
** The format summary, and the parmlib line that mounts what was just
** built.  OWNER() carries the owner the disk was formatted for, which
** is the documented way of handing a disk to a userid -- and is left
** out of the suggestion entirely when the disk was formatted without
** one, so the line stays copyable (#62).
** ============================================================ */
static void
report(const UFSFMT_PARMS *p, const UFSFMT_GEOM *g, const char *dsn)
{
    unsigned data_blocks;

    if (s_quiet) return;

    data_blocks = g->total_blocks - g->datablock_start;

    /* A blank line has to carry a character: SYSPRINT records are
    ** written with carriage control, and an empty one is folded into
    ** the record that follows -- which would push the message id off
    ** column 1, where operators and automation look for it. */
    printf(" \n");
    printf("UFSFMT80I Format summary\n");
    printf("UFSFMT81I   Dataset . . . . . %s\n", dsn[0] ? dsn : p->ddname);
    printf("UFSFMT82I   Block size  . . . %u\n", g->blksize);
    printf("UFSFMT83I   Total blocks  . . %-14u(%.2f MB)\n",
           g->total_blocks, fmt_mb(g->total_blocks, g->blksize));
    printf("UFSFMT84I   Inode blocks  . . %-14u(%u slots, %u free)\n",
           g->inode_blocks, g->inode_slots, g->total_freeinode);
    printf("UFSFMT85I   Data blocks . . . %-14u(%u free)\n",
           data_blocks, g->total_freeblock);
    printf("UFSFMT86I   Root owner  . . . %s/%s\n",
           ufsfmt_owner_text(p->owner), ufsfmt_owner_text(p->group));
    printf(" \n");
    printf("UFSFMT90I Add to your UFSD parmlib member:\n");
    {
        char stmt[128];

        ufsfmt_mount_stmt(stmt, sizeof(stmt), dsn, p->owner);
        printf("UFSFMT91I   %s\n", stmt);
    }
}
