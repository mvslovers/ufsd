/* TSTUFSIO.C - the ferror() guards on UFSD's two stdio read loops (#69)
**
** MVS-only.  Since libc370 1.0.4 an uncorrectable read error comes back
** from fgets() as NULL, exactly like end of file.  Two loops read it that
** way and were guarded with ferror():
**
**   ufsd_cfg_read()   src/ufsd#cfg.c    DD:UFSDPRM   UFSD106E, returns 8
**   parse_sysin()     src/ufsfmt.c      stdin/SYSIN  UFSFMT17E, returns 8
**
** The vector is a wrong-length block (as in libc370's tstsynad.c): a data
** set whose first block is 80 bytes and whose second is 3120, read through
** a DD whose DCB says RECFM=F,LRECL=80,BLKSIZE=80.  The first READ is
** fine and delivers the first control statement, the second READ meets a
** block that does not fit and fails.  Without the guard the loop ends as
** at end of file with the first statement in hand and the function says
** "success": the ROOT of the parmlib is known, the rest of the MOUNT list
** is missing, and UFSD would come up looking healthy.  So the first
** statement is the one that, taken alone, makes the unguarded code pass.
**
** The data sets are temporary and every one is written by this
** program, block by block, so no fixture and no hand-written JCL is needed.
** Each case runs twice: with a well-formed data set (the guard must stay
** quiet and the whole input must be parsed) and with the broken one.
**
** parse_sysin() is static, so src/ufsfmt.c is included whole with its
** main() renamed.  A static in an AC(0) test is fine; the module-data rule
** applies to the AC(1) load modules.
*/

#include <mbtcheck.h>
#include <dynit.h>
#include <errno.h>
#include <string.h>
#include <mvs/osio.h>
#include <mvs/dd.h>
#include <mvs/wto.h>

#define main fmtmain
#include "../../src/ufsfmt.c"
#undef main

#define CARD     80
#define BIGBLK   3120

static int
alloc_dd(const char *ddn, const char *dsn, const char *volser, char status,
         int new_ds, short recfm, short blksize, unsigned short lrecl)
{
    __dyn_t dyn;
    int     rc;

    dyninit(&dyn);
    dyn.__ddname   = (char *)ddn;
    dyn.__dsname   = (char *)dsn;
    dyn.__status   = status;
    dyn.__normdisp = __DISP_DELETE;
    dyn.__conddisp = __DISP_DELETE;
    dyn.__recfm    = recfm;
    dyn.__blksize  = blksize;
    dyn.__lrecl    = lrecl;
    if (volser) {
        dyn.__unit      = "SYSDA";
        dyn.__volser    = (char *)volser;
    }
    if (new_ds) {
        dyn.__unit      = "SYSDA";
        dyn.__alcunit   = __TRK;
        dyn.__primary   = 1;
        dyn.__secondary = 0;
        dyn.__dsorg     = __DSORG_PS;
    }
    rc = dynalloc(&dyn);
    if (rc)
        wtof("TSTUFSIO dynalloc %s %s: rc=%d error=%04X info=%04X", ddn, dsn,
               rc, (unsigned)(unsigned short)dyn.__errcode,
               (unsigned)(unsigned short)dyn.__infocode);
    return rc;
}

static void
free_dd(const char *ddn)
{
    __dyn_t dyn;

    dyninit(&dyn);
    dyn.__ddname = (char *)ddn;
    dynfree(&dyn);
}

/* One block of the data set: len bytes, text then blanks. */
static int
put_block(DCB *dcb, const char *text, int len)
{
    static char blk[BIGBLK];
    DECB        decb;

    memset(blk, ' ', sizeof(blk));
    memcpy(blk, text, strlen(text));
    oswrite(&decb, dcb, blk, len);
    return oscheck(&decb);
}

/* A data set made by make_ds(): the name and volume the temporary data set
** got, which is all a second DD needs to reach it. */
typedef struct {
    char dsn[45];
    char vol[7];
} TMPDS;

/* Write a temporary data set of two blocks as RECFM=U: the first holds
** `first` in 80 bytes, the second `second` in 80 bytes or, for the broken
** one, 3120.  The writer DD stays allocated so the temporary data set
** lives until the end of the step; it is the data set's only owner, and
** the reading DDs below point at it by the name and volume it was given. */
static int
make_ds(const char *wdd, const char *dsn, const char *first,
        const char *second, int broken, TMPDS *out)
{
    DCB  *dcb;
    JFCB  jfcb;
    int   ok;
    int   i;

    if (alloc_dd(wdd, dsn, NULL, __DISP_NEW, 1, _U_, BIGBLK, 0)) return -1;

    dcb = osbdcb(wdd, NULL);
    if (!dcb) return -2;
    if (osbopen(dcb, 0, "write")) return -3;

    ok = put_block(dcb, first, CARD) == 0
      && put_block(dcb, second, broken ? BIGBLK : CARD) == 0;

    memset(&jfcb, 0, sizeof(jfcb));
    if (rdjfcb(dcb, &jfcb)) ok = 0;
    osbclose(dcb, NULL, 1, 0);

    memset(out, 0, sizeof(*out));
    memcpy(out->dsn, jfcb.jfcbdsnm, 44);
    for (i = 43; i >= 0 && out->dsn[i] == ' '; i--) out->dsn[i] = '\0';
    memcpy(out->vol, jfcb.jfcbvols, 6);
    out->vol[6] = '\0';
    return ok && out->dsn[0] && out->vol[0] ? 0 : -4;
}

/* The reading DD: RECFM=F,LRECL=80,BLKSIZE=80 over the same data set. */
static int
read_dd(const char *ddn, const TMPDS *ds)
{
    return alloc_dd(ddn, ds->dsn, ds->vol, __DISP_SHR, 0, _F_, CARD, CARD);
}

static void
cfg_cases(void)
{
    static UFSD_CONFIG cfg;
    TMPDS good, bad;
    int rc;

    printf("--- ufsd_cfg_read(): DD:UFSDPRM ---\n");

    CHECK(make_ds("TSTCGW", NULL,
                  "ROOT DSN(TEST.ROOT) BLKSIZE(4096)",
                  "MOUNT DSN(TEST.HOME) PATH(/u/test) MODE(RW)", 0, &good) == 0,
          "cfg: good data set written");
    CHECK(make_ds("TSTCBW", NULL,
                  "ROOT DSN(TEST.ROOT) BLKSIZE(4096)",
                  "MOUNT DSN(TEST.HOME) PATH(/u/test) MODE(RW)", 1, &bad) == 0,
          "cfg: broken data set written");

    CHECK(read_dd("UFSDPRM", &good) == 0, "cfg: good DD allocated");
    rc = ufsd_cfg_read(&cfg);
    CHECK_EQ(rc, 0, "cfg good: ufsd_cfg_read returns 0");
    CHECK_EQ(cfg.nmounts, 1, "cfg good: both statements parsed (1 MOUNT)");
    CHECK(strcmp(cfg.root_dsname, "TEST.ROOT") == 0,
          "cfg good: ROOT parsed");
    free_dd("UFSDPRM");

    CHECK(read_dd("UFSDPRM", &bad) == 0, "cfg: broken DD allocated");
    rc = ufsd_cfg_read(&cfg);
    CHECK(strcmp(cfg.root_dsname, "TEST.ROOT") == 0,
          "cfg broken: ROOT was read before the error (vector reaches the guard)");
    CHECK_EQ(cfg.nmounts, 0, "cfg broken: the MOUNT after the error is missing");
    CHECK_EQ(rc, 8, "cfg broken: ufsd_cfg_read returns 8 (UFSD106E)");
    free_dd("UFSDPRM");

    free_dd("TSTCGW");
    free_dd("TSTCBW");
}

static void
sysin_cases(void)
{
    UFSFMT_PARMS p;
    TMPDS        good, bad;
    int          rc;

    printf("--- parse_sysin(): SYSIN ---\n");

    CHECK(make_ds("TSTSGW", NULL, "BLKSIZE 2048", "OWNER TESTER", 0, &good) == 0,
          "sysin: good data set written");
    CHECK(make_ds("TSTSBW", NULL, "BLKSIZE 2048", "OWNER TESTER", 1, &bad) == 0,
          "sysin: broken data set written");

    CHECK(read_dd("TSTSIN1", &good) == 0, "sysin: good DD allocated");
    CHECK(freopen("DD:TSTSIN1", "r", stdin) != NULL, "sysin good: stdin opened");
    memset(&p, 0, sizeof(p));
    strcpy(p.ddname, "DISKFILE");
    p.blksize = 4096U;
    p.inode_pct = 10.0;
    rc = parse_sysin(&p);
    CHECK_EQ(rc, 0, "sysin good: parse_sysin returns 0");
    CHECK_EQ(p.blksize, 2048U, "sysin good: BLKSIZE parsed");
    CHECK(strcmp(p.owner, "TESTER") == 0, "sysin good: OWNER parsed");

    /* freopen() closes the good stream; its DD can go after that. */
    CHECK(read_dd("TSTSIN2", &bad) == 0, "sysin: broken DD allocated");
    CHECK(freopen("DD:TSTSIN2", "r", stdin) != NULL, "sysin broken: stdin opened");
    free_dd("TSTSIN1");
    memset(&p, 0, sizeof(p));
    strcpy(p.ddname, "DISKFILE");
    p.blksize = 4096U;
    p.inode_pct = 10.0;
    errno = 0;
    rc = parse_sysin(&p);
    CHECK_EQ(p.blksize, 2048U,
             "sysin broken: BLKSIZE was read before the error (vector reaches the guard)");
    CHECK_EQ(rc, 8, "sysin broken: parse_sysin returns 8 (UFSFMT17E)");
    CHECK(ferror(stdin) != 0, "sysin broken: stdin is flagged ferror");
    CHECK(feof(stdin) == 0, "sysin broken: stdin is not at end of file");
    CHECK_EQ(errno, EIO, "sysin broken: errno is EIO");
    fclose(stdin);
    free_dd("TSTSIN2");

    free_dd("TSTSGW");
    free_dd("TSTSBW");
}

int
main(void)
{
    printf("=== ferror() guards on the stdio read loops (#69) ===\n");

    cfg_cases();
    sysin_cases();

    return mbt_test_summary("TSTUFSIO");
}
