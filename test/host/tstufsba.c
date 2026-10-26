/* TSTUFSBA.C - no block is handed out twice (#169)
**
** Host-only.  A client writes files through libufs; the request goes to the
** real file layer (src/ufsd#fil.c), directory layer (src/ufsd#dir.c), inode
** layer (src/ufsd#ino.c) and superblock/allocator (src/ufsd#sbl.c).  Only the
** two ends are stand-ins: iefssreq() below plays the SSI router and
** ufsd_dispatch's response copying, and ufsd_blk_read/write keep the volume
** in a RAM image.  The volume is formatted the way UFSFMT does it, from
** ufsfmt_geometry().
**
** The property under test: after every batch of writes
**
**   - each block read back equals the block written,
**   - no block is referenced by two inodes (data blocks and indirect blocks),
**   - no block in the superblock's free cache is referenced by an inode, and
**     none is in the cache twice.
**
** Why it failed: the free block scan only counts the blocks an inode on disk
** points to.  do_fwrite keeps the blocks it assigns in its in-memory copy of
** the inode until the call ends, and the block alloc just popped is in no
** inode at all, so a scan that ran inside the alloc handed those blocks out
** again.  The scan runs whenever the cache cannot be refilled from a V7 chain
** block: on a volume whose chain is gone (formatted by an old UFSD, or after
** the first scan), and after blocks were freed.  Both volume states are run.
**
** libufs sends at most 4096 bytes per call, and the server refuses a count
** above the 4096 bytes of a request buffer.  A call still takes several
** blocks whenever the volume's block size is below 4096 (eight blocks of 512
** bytes per call, direct and indirect) or the call does not start on a block
** boundary (two blocks of 4096), so those are run as well.  test_full_volume()
** runs a call into a volume with fewer free blocks than the call needs.
**
** The image and the counters are file statics: this is a test, not an AC(1)
** module.
*/

#include <mbtcheck.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "libufs.h"
#include "ufsd.h"
#include "ufsfmt.h"
#include <ibm/mvs/iefssobh.h>
#include <mvs/subsys.h>

#define MAXBLK    4096U
#define TOKEN     1U

static unsigned char *g_img;
static unsigned       g_nblk;
static unsigned       g_blk;            /* block size of the volume         */
static unsigned       g_scans;          /* UFSD070I: free block scans run   */

/* ============================================================
** Stand-ins below the file layer
** ============================================================ */

int
ufsd_blk_read(UFSD_DISK *disk, unsigned sector, void *buf)
{
    (void)disk;
    if (sector >= g_nblk) return UFSD_RC_IO;
    memcpy(buf, g_img + (size_t)sector * g_blk, g_blk);
    return UFSD_RC_OK;
}

int
ufsd_blk_write(UFSD_DISK *disk, unsigned sector, void *buf)
{
    (void)disk;
    if (sector >= g_nblk) return UFSD_RC_IO;
    memcpy(g_img + (size_t)sector * g_blk, buf, g_blk);
    return UFSD_RC_OK;
}

void
wtof(const char *text, ...)
{
    if (strncmp(text, "UFSD070I", 8) == 0) g_scans++;
}

void mtime64(mtime64_t *t) { memset(t, 0, sizeof *t); }
void __64_from_u32(__64 *r, unsigned v) { (void)v; memset(r, 0, sizeof *r); }
void __64_mul_u32(__64 *a, unsigned v, __64 *r) { (void)a; (void)v; (void)r; }
void __64_add_u32(__64 *a, unsigned v, __64 *r) { (void)a; (void)v; (void)r; }
int  ufsd_mount_child(const UFSD_MOUNTPT *t, unsigned n, int p, unsigned i)
{ (void)t; (void)n; (void)p; (void)i; return -1; }

/* The real ufsd#gft.c enters supervisor state for its init; these two are
** its plain table walk. */
int
ufsd_gft_alloc(UFSD_ANCHOR *anchor, unsigned *out_idx)
{
    unsigned i;

    for (i = 0; i < anchor->max_gfiles; i++) {
        if (!(anchor->gfiles[i].flags & UFSD_GF_USED)) {
            memset(&anchor->gfiles[i], 0, sizeof(UFSD_GFILE));
            anchor->gfiles[i].flags = UFSD_GF_USED;
            *out_idx = i;
            return UFSD_RC_OK;
        }
    }
    return UFSD_RC_NOREQ;
}

void
ufsd_gft_release(UFSD_ANCHOR *anchor, unsigned idx)
{
    memset(&anchor->gfiles[idx], 0, sizeof(UFSD_GFILE));
}

/* ============================================================
** Fixture: a volume formatted like UFSFMT does it
** ============================================================ */

static UFSD_DISK    g_disk;
static UFSD_STC     g_stc;
static UFSD_ANCHOR  g_anchor;
static UFSD_SESSION g_sess;
static UFSD_UFS     g_ufs;
static UFSD_GFILE   g_gfiles[UFSD_MAX_GFILES];

static unsigned char *
sector(unsigned s)
{
    return g_img + (size_t)s * g_blk;
}

/* chain != 0: the V7 free chain UFSFMT writes.  chain == 0: no chain blocks,
** which is what a volume looks like once an old UFSD ran on it; every cache
** refill is then a scan. */
static void
format(unsigned blksize, unsigned nblk, int chain)
{
    UFSFMT_GEOM  g;
    UFSD_DINODE *root;
    UFSD_DIRENT *de;
    UFSD_SB     *sb;
    struct { unsigned n; unsigned blk[UFSFMT_MAX_FREEBLOCK]; } ch;
    unsigned     b, cs, i;
    int          rc;

    free(g_img);
    g_blk  = blksize;
    g_img  = (unsigned char *)calloc(nblk, g_blk);
    g_nblk = nblk;

    rc = ufsfmt_geometry(&g, nblk, g_blk, 50.0);
    CHECK_EQ(rc, UFSFMT_GEOM_OK, "geometry of the test volume");

    memset(sector(UFSFMT_ILIST_SECTOR), 0xFF, 2U * UFSFMT_INODE_SIZE);

    /* Free block chain: as format_disk() in src/ufsfmt.c. */
    cs = g.datablock_start;
    memset(&ch, 0, sizeof ch);
    for (b = g.chain_start; b < g.total_blocks; b++) {
        ch.blk[ch.n++] = b;
        if (ch.n == UFSFMT_MAX_FREEBLOCK || b + 1U == g.total_blocks) {
            if (chain) memcpy(sector(cs), &ch, sizeof ch);
            cs = ch.blk[0];
            memset(&ch, 0, sizeof ch);
        }
    }

    /* Root inode (slot 1 of the first inode block) and its directory. */
    root = (UFSD_DINODE *)(sector(UFSFMT_ILIST_SECTOR) + UFSFMT_INODE_SIZE);
    memset(root, 0, sizeof *root);
    root->mode     = (unsigned short)(UFSD_IFDIR | 0755U);
    root->nlink    = 2;
    root->filesize = 2U * UFSFMT_DIRENT_SIZE;
    root->addr[0]  = g.root_block;
    de = (UFSD_DIRENT *)sector(g.root_block);
    de[0].ino = UFSFMT_ROOT_INO;  strcpy(de[0].name, ".");
    de[1].ino = UFSFMT_ROOT_INO;  strcpy(de[1].name, "..");

    sb = (UFSD_SB *)sector(1U);
    sb->datablock_start  = g.datablock_start;
    sb->volume_size      = g.total_blocks;
    sb->total_freeblock  = g.total_freeblock;
    sb->total_freeinode  = g.total_freeinode;
    sb->nfreeblock       = g.nfreeblock;
    sb->nfreeinode       = g.nfreeinode;
    sb->inodes_per_block = g.inodes_per_block;
    sb->blksize_shift    = g.blksize_shift;
    sb->ilist_sector     = UFSFMT_ILIST_SECTOR;
    for (i = 0; i < UFSFMT_MAX_FREEBLOCK; i++) sb->freeblock[i] = g.freeblock[i];
    for (i = 0; i < UFSFMT_MAX_FREEINODE; i++) sb->freeinode[i] = g.freeinode[i];

    memset(&g_disk, 0, sizeof g_disk);
    g_disk.blksize    = g_blk;
    g_disk.flags      = UFSD_DISK_OPEN | UFSD_DISK_ROOT;
    g_disk.mount_mode = UFSD_MOUNT_RW;
    strcpy(g_disk.mountpath, "/");
    memcpy(&g_disk.sb, sb, sizeof *sb);

    memset(&g_stc, 0, sizeof g_stc);
    g_stc.ndisks   = 1;
    g_stc.disks[0] = &g_disk;

    memset(g_gfiles, 0, sizeof g_gfiles);
    memset(&g_anchor, 0, sizeof g_anchor);
    g_anchor.gfiles     = g_gfiles;
    g_anchor.max_gfiles = UFSD_MAX_GFILES;
    g_anchor.server_stc = &g_stc;

    memset(&g_ufs, 0, sizeof g_ufs);
    strcpy(g_ufs.cwd, "/");
    g_ufs.cwd_ino  = UFSD_ROOT_INO;
    g_ufs.disk_idx = 0;

    memset(&g_sess, 0, sizeof g_sess);
    memcpy(g_sess.eye, "UFSDSESS", 8);
    g_sess.token = TOKEN;
    g_sess.flags = UFSD_SESS_ACTIVE;
    g_sess.ufs   = &g_ufs;
    for (i = 0; i < UFSD_MAX_FD; i++)
        g_sess.fd_table[i].gfile_idx = UFSD_FD_UNUSED;

    g_scans = 0;
}

/* ============================================================
** The SSI router stand-in: what ufsd_dispatch and the router do with a
** request, minus the key-0 window and the cross-address-space post.
** ============================================================ */

static int
serve(UFSSSOB *u)
{
    UFSREQ  *req;
    char     resp[UFSREQ_MAX_INLINE];
    unsigned rlen = 0;
    int      rc;

    if (u->func == UFSREQ_SESS_OPEN) {
        *(unsigned *)u->data = TOKEN;
        u->data_len = (unsigned)sizeof(unsigned);
        u->rc       = UFSD_RC_OK;
        return SSRTOK;
    }
    if (u->func == UFSREQ_SESS_CLOSE) {
        u->data_len = 0;
        u->rc       = UFSD_RC_OK;
        return SSRTOK;
    }

    req = (UFSREQ *)calloc(1, sizeof *req);
    memcpy(req->eye, "UFSREQ__", 8);
    req->func          = u->func;
    req->session_token = u->token;
    req->data_len      = u->data_len;
    memcpy(req->data, u->data, sizeof req->data);

    if (u->func == UFSREQ_FWRITE && u->buf_ptr != NULL) {
        req->buf = (UFSBUF *)calloc(1, sizeof(UFSBUF));
        memcpy(req->buf->data, u->buf_ptr, u->buf_len);
    }
    if (u->func == UFSREQ_FREAD && u->data_len >= 8U
        && *(unsigned *)(u->data + 4) > UFSREQ_MAX_INLINE - 4U)
        req->buf = (UFSBUF *)calloc(1, sizeof(UFSBUF));

    memset(resp, 0, sizeof resp);
    rc = ufsd_fil_dispatch(&g_anchor, &g_sess, req, resp, &rlen);

    u->rc       = rc;
    u->data_len = 0;
    if (u->func == UFSREQ_FREAD && rc == UFSD_RC_OK && rlen == 4U) {
        unsigned bread = *(unsigned *)resp;
        char    *stg   = *(char **)(resp + 4);

        memcpy(u->data, resp, 4U);
        u->data_len = 4U;
        if (stg != NULL && bread > 0 && u->buf_ptr != NULL)
            memcpy(u->buf_ptr, stg, bread);
        free(stg);
    } else if (rlen > 0) {
        u->data_len = rlen;
        memcpy(u->data, resp, rlen);
    }

    free(req->buf);
    free(req);
    return SSRTOK;
}

int
iefssreq(SSOB *ssob)
{
    return serve((UFSSSOB *)ssob->SSOBINDV);
}

/* ============================================================
** Content and audit
** ============================================================ */

static void
fill(unsigned char *p, unsigned len, int file, int salt)
{
    unsigned x = (unsigned)(file * 7919 + salt * 104729 + 1);
    unsigned k;

    for (k = 0; k < len; k++) {
        x = x * 1103515245U + 12345U;
        p[k] = (unsigned char)(x >> 16);
    }
}

typedef struct audit {
    unsigned shared;        /* blocks referenced by more than one inode slot */
    unsigned cache_used;    /* cache entries an inode references            */
    unsigned cache_twice;   /* cache entries that are in the cache twice    */
} AUDIT;

static void
audit(AUDIT *a)
{
    unsigned char *refs  = (unsigned char *)calloc(g_nblk, 1);
    unsigned char *seen  = (unsigned char *)calloc(g_nblk, 1);
    unsigned       ipb   = g_disk.sb.inodes_per_block;
    unsigned       nino  = (g_disk.sb.datablock_start
                            - g_disk.sb.ilist_sector) * ipb;
    unsigned       ino, i, b;
    UFSD_DINODE    d;
    unsigned       ind[MAXBLK / 4U];

    memset(a, 0, sizeof *a);
    for (ino = 2; ino <= nino; ino++) {
        if (ufsd_ino_read(&g_disk, ino, &d) != UFSD_RC_OK) continue;
        if (d.mode == 0) continue;
        for (i = 0; i < 17U; i++)               /* 16 direct + the indirect */
            if (d.addr[i] != 0 && d.addr[i] < g_nblk) refs[d.addr[i]]++;
        if (d.addr[16] != 0 && d.addr[16] < g_nblk) {
            memcpy(ind, sector(d.addr[16]), g_blk);
            for (i = 0; i < g_blk / 4U; i++)
                if (ind[i] != 0 && ind[i] < g_nblk) refs[ind[i]]++;
        }
    }
    for (b = 0; b < g_nblk; b++)
        if (refs[b] > 1) a->shared++;
    for (i = 0; i < g_disk.sb.nfreeblock; i++) {
        b = g_disk.sb.freeblock[i];
        if (b >= g_nblk) continue;
        if (refs[b]) a->cache_used++;
        if (seen[b]++) a->cache_twice++;
    }
    free(refs);
    free(seen);
}

/* ============================================================
** What a test run is made of
** ============================================================ */

typedef struct run {
    const char *label;
    unsigned    blksize;    /* volume block size                        */
    unsigned    nblk;       /* volume size in blocks                    */
    int         chain;      /* volume carries the V7 free chain         */
    int         nfiles;
    unsigned    fbytes;     /* bytes per file                           */
    unsigned    chunk;      /* bytes per ufs_fwrite call                */
    unsigned    skew;       /* a first call of this many bytes, so the
                            ** calls that follow start off a boundary  */
} RUN;

static void
note_worst(AUDIT *worst, const AUDIT *a)
{
    if (a->shared      > worst->shared)      worst->shared      = a->shared;
    if (a->cache_used  > worst->cache_used)  worst->cache_used  = a->cache_used;
    if (a->cache_twice > worst->cache_twice) worst->cache_twice = a->cache_twice;
}

/* Writes the files in batches; each batch is audited, read back block by
** block and removed.  Returns the number of blocks that read back wrong. */
static unsigned
run_files(UFS *ufs, const RUN *r, AUDIT *worst, unsigned *errors)
{
    unsigned char *buf  = (unsigned char *)malloc(r->fbytes);
    unsigned char *want = (unsigned char *)malloc(r->fbytes);
    unsigned       bad = 0;
    int            b0, i;
    unsigned       b;
    char           path[32];
    AUDIT          a;

    memset(worst, 0, sizeof *worst);
    for (b0 = 0; b0 < r->nfiles; b0 += 20) {
        for (i = b0; i < b0 + 20 && i < r->nfiles; i++) {
            UFSFILE *fp;
            unsigned off = 0, n;

            fill(want, r->fbytes, i, 1);
            sprintf(path, "/tmp/t%d", i);
            fp = ufs_fopen(ufs, path, "w");
            if (!fp) { (*errors)++; continue; }
            while (off < r->fbytes) {
                n = off == 0 && r->skew != 0 ? r->skew : r->chunk;
                if (n > r->fbytes - off) n = r->fbytes - off;
                if (ufs_fwrite(want + off, 1, n, fp) < n) {
                    (*errors)++;
                    break;
                }
                off += n;
            }
            ufs_fclose(&fp);
        }

        audit(&a);
        note_worst(worst, &a);

        for (i = b0; i < b0 + 20 && i < r->nfiles; i++) {
            UFSFILE *fp;
            unsigned got = 0, n;

            fill(want, r->fbytes, i, 1);
            sprintf(path, "/tmp/t%d", i);
            fp = ufs_fopen(ufs, path, "r");
            if (!fp) { (*errors)++; continue; }
            while (got < r->fbytes
                   && (n = ufs_fread(buf + got, 1, r->fbytes - got, fp)) > 0)
                got += n;
            ufs_fclose(&fp);
            if (got != r->fbytes) (*errors)++;
            for (b = 0; b * g_blk < r->fbytes; b++)
                if (memcmp(buf + (size_t)b * g_blk, want + (size_t)b * g_blk,
                           g_blk) != 0)
                    bad++;
        }

        for (i = b0; i < b0 + 20 && i < r->nfiles; i++) {
            sprintf(path, "/tmp/t%d", i);
            if (ufs_remove(ufs, path) != UFSD_RC_OK) (*errors)++;
        }
    }
    free(buf);
    free(want);
    return bad;
}

static UFS *
start(unsigned blksize, unsigned nblk, int chain)
{
    UFS *ufs;

    format(blksize, nblk, chain);
    ufs = ufsnew();
    if (ufs == NULL) return NULL;
    if (ufs_mkdir(ufs, "/tmp") != UFSD_RC_OK) {
        ufsfree(&ufs);
        return NULL;
    }
    return ufs;
}

static void
scenario(const RUN *r)
{
    char     msg[160];
    UFS     *ufs = start(r->blksize, r->nblk, r->chain);
    AUDIT    a;
    unsigned errors = 0, bad;

    CHECK(ufs != NULL, "session and /tmp");
    if (ufs == NULL) return;

    bad = run_files(ufs, r, &a, &errors);
    printf("  %s: %u scans, %u wrong blocks, %u shared, %u cached+used, "
           "%u cached twice, %u errors\n",
           r->label, g_scans, bad, a.shared, a.cache_used, a.cache_twice,
           errors);

    sprintf(msg, "%s: no request failed", r->label);
    CHECK_EQ(errors, 0, msg);
    sprintf(msg, "%s: every block reads back as written", r->label);
    CHECK_EQ(bad, 0, msg);
    sprintf(msg, "%s: no block referenced twice", r->label);
    CHECK_EQ(a.shared, 0, msg);
    sprintf(msg, "%s: no free-cache block is in use", r->label);
    CHECK_EQ(a.cache_used, 0, msg);
    sprintf(msg, "%s: no block in the free cache twice", r->label);
    CHECK_EQ(a.cache_twice, 0, msg);
    if (!r->chain) {
        sprintf(msg, "%s: the scan path ran", r->label);
        CHECK(g_scans > 0, msg);
    }
    ufsfree(&ufs);
}

#define KB32   32768U
#define KB96   98304U

static void
test_libufs_calls(void)
{
    static const RUN runs[] = {
        /* 4096-byte blocks: one block per call, 200 files of 8 blocks.  */
        { "4K blocks, chain, calls of 200",    4096, 1400, 1, 200, KB32,  200, 0 },
        { "4K blocks, no chain, calls of 200", 4096, 1400, 0, 200, KB32,  200, 0 },
        { "4K blocks, chain, calls of 256",    4096, 1400, 1, 200, KB32,  256, 0 },
        { "4K blocks, no chain, calls of 256", 4096, 1400, 0, 200, KB32,  256, 0 },
        { "4K blocks, chain, calls of 3000",   4096, 1400, 1, 200, KB32, 3000, 0 },
        { "4K blocks, no chain, calls of 3000",4096, 1400, 0, 200, KB32, 3000, 0 },
        { "4K blocks, chain, calls of 4096",   4096, 1400, 1, 200, KB32, 4096, 0 },
        { "4K blocks, no chain, calls of 4096",4096, 1400, 0, 200, KB32, 4096, 0 },
        /* 24 blocks per file: the indirect block comes into play.       */
        { "4K blocks, no chain, 24-block files, calls of 4096",
                                               4096, 1400, 0,  40, KB96, 4096, 0 },
        /* calls off a block boundary: every call takes two blocks.      */
        { "4K blocks, no chain, calls of 4096 off the boundary",
                                               4096, 1400, 0, 200, KB32, 4096, 100 },
        /* 512-byte blocks: every call of 4096 takes eight blocks, and a
        ** 32 KB file is 64 blocks, direct and indirect.                 */
        { "512-byte blocks, chain, calls of 4096",
                                                512, 8000, 1, 200, KB32, 4096, 0 },
        { "512-byte blocks, no chain, calls of 4096",
                                                512, 8000, 0, 200, KB32, 4096, 0 },
        { "512-byte blocks, no chain, calls of 4096 off the boundary",
                                                512, 8000, 0, 200, KB32, 4096, 100 },
    };
    unsigned i;

    for (i = 0; i < sizeof runs / sizeof runs[0]; i++)
        scenario(&runs[i]);
}

/* ============================================================
** Calls on the wire, below libufs
** ============================================================ */

static unsigned unreferenced_blocks(void);
static int
srv_open(const char *path)
{
    UFSSSOB u;

    memset(&u, 0, sizeof u);
    u.func = UFSREQ_FOPEN;
    u.token = TOKEN;
    *(unsigned *)u.data = UFSD_OPEN_WRITE;
    strcpy(u.data + 4, path);
    u.data_len = 4U + (unsigned)strlen(path) + 1U;
    serve(&u);
    return u.rc == UFSD_RC_OK ? *(int *)u.data : -1;
}

static void
srv_close(int fd)
{
    UFSSSOB u;

    memset(&u, 0, sizeof u);
    u.func = UFSREQ_FCLOSE;
    u.token = TOKEN;
    *(unsigned *)u.data = (unsigned)fd;
    u.data_len = 4U;
    serve(&u);
}

/* One FWRITE of `count` bytes in a request buffer of `bufsize` bytes.
** Returns the rc; *written is the byte count the server answers. */
static int
srv_write(int fd, const unsigned char *data, unsigned count,
          unsigned bufsize, unsigned *written)
{
    UFSREQ  *req = (UFSREQ *)calloc(1, sizeof *req);
    UFSBUF  *buf = (UFSBUF *)calloc(1, offsetof(UFSBUF, data) + bufsize);
    char     resp[UFSREQ_MAX_INLINE];
    unsigned rlen = 0;
    int      rc;

    memcpy(req->eye, "UFSREQ__", 8);
    req->func          = UFSREQ_FWRITE;
    req->session_token = TOKEN;
    req->data_len      = 8U;
    *(unsigned *)req->data       = (unsigned)fd;
    *(unsigned *)(req->data + 4) = count;
    memcpy(buf->data, data, count < bufsize ? count : bufsize);
    req->buf = buf;

    memset(resp, 0, sizeof resp);
    rc = ufsd_fil_dispatch(&g_anchor, &g_sess, req, resp, &rlen);
    *written = *(unsigned *)resp;
    free(buf);
    free(req);
    return rc;
}

/* Blocks no inode references, not counting the boot, super and inode list
** blocks: the blocks a scan can hand out. */
static unsigned
unreferenced_blocks(void)
{
    unsigned char *refs = (unsigned char *)calloc(g_nblk, 1);
    unsigned       ipb  = g_disk.sb.inodes_per_block;
    unsigned       nino = (g_disk.sb.datablock_start
                           - g_disk.sb.ilist_sector) * ipb;
    unsigned       ino, i, b, n = 0;
    UFSD_DINODE    d;
    unsigned       ind[MAXBLK / 4U];

    for (b = 0; b < g_disk.sb.datablock_start; b++) refs[b] = 1;
    for (ino = 2; ino <= nino; ino++) {
        if (ufsd_ino_read(&g_disk, ino, &d) != UFSD_RC_OK || d.mode == 0)
            continue;
        for (i = 0; i < 17U; i++)
            if (d.addr[i] != 0 && d.addr[i] < g_nblk) refs[d.addr[i]] = 1;
        if (d.addr[16] != 0 && d.addr[16] < g_nblk) {
            memcpy(ind, sector(d.addr[16]), g_blk);
            for (i = 0; i < g_blk / 4U; i++)
                if (ind[i] != 0 && ind[i] < g_nblk) refs[ind[i]] = 1;
        }
    }
    for (b = 0; b < g_nblk; b++)
        if (!refs[b]) n++;
    free(refs);
    return n;
}

/* The request buffer holds 4096 bytes and the server holds the client to
** that.  If this limit ever goes, calls of many blocks become possible and
** the property tests above must grow to cover them. */
static void
test_oversize_refused(void)
{
    UFS           *ufs = start(4096U, 1400U, 0);
    unsigned char *data = (unsigned char *)calloc(1, 8192U);
    unsigned       before, w = 99;
    int            fd, rc;

    CHECK(ufs != NULL, "oversize: session and /tmp");
    if (!ufs) return;

    before = unreferenced_blocks();
    fd = srv_open("/tmp/big");
    rc = srv_write(fd, data, 8192U, 8192U, &w);
    srv_close(fd);
    CHECK_EQ(rc, UFSD_RC_INVALID, "oversize: a count above 4096 is refused");
    CHECK_EQ(w, 0, "oversize: nothing is reported written");
    CHECK_EQ(unreferenced_blocks(), before, "oversize: no block was taken");
    ufsfree(&ufs);
    free(data);
}

/* A call that needs more blocks than are left stops with NOSPACE and keeps
** what it wrote; none of the blocks it took is handed out a second time. */
static void
test_full_volume(void)
{
    UFS            *ufs;
    unsigned char  *data = (unsigned char *)malloc(4096U);
    unsigned        w = 0, n = 0, nfile = 0;
    int             fd, rc;
    AUDIT           a;
    char            path[32];

    ufs = start(512U, 400U, 0);
    CHECK(ufs != NULL, "full volume: session and /tmp");
    if (!ufs) return;
    fill(data, 4096U, 1, 1);

    /* Leave exactly 5 blocks.  Files of 8 blocks stay direct-only; the
    ** blocks go one at a time so the count lands on 5. */
    while (unreferenced_blocks() > 5U && nfile < 100U) {
        sprintf(path, "/tmp/f%u", nfile++);
        fd = srv_open(path);
        for (n = 0; n < 8U && unreferenced_blocks() > 5U; n++) {
            rc = srv_write(fd, data, 512U, 4096U, &w);
            if (rc != UFSD_RC_OK) break;
        }
        srv_close(fd);
    }
    g_disk.sb.nfreeblock = 0;                   /* the next alloc scans */
    CHECK_EQ(unreferenced_blocks(), 5, "full volume: 5 blocks left");

    fd = srv_open("/tmp/over");
    rc = srv_write(fd, data, 4096U, 4096U, &w);
    srv_close(fd);
    printf("  full volume: 8 blocks asked, 5 left: rc %d, %u bytes written\n",
           rc, w);
    CHECK_EQ(rc, UFSD_RC_NOSPACE, "full volume: the call ends with NOSPACE");
    CHECK_EQ(w, 5U * 512U, "full volume: exactly the 5 free blocks written");
    CHECK_EQ(unreferenced_blocks(), 0, "full volume: no block left over");

    audit(&a);
    CHECK_EQ(a.shared, 0, "full volume: no block referenced twice");
    CHECK_EQ(a.cache_used, 0, "full volume: no free-cache block is in use");
    ufsfree(&ufs);
    free(data);
}

int
main(void)
{
    printf("TSTUFSBA: block allocation (#169)\n");
    test_libufs_calls();
    test_oversize_refused();
    test_full_volume();
    return mbt_test_summary("TSTUFSBA");
}
