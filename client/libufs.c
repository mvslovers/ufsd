/* LIBUFS.C - UFS API Client Stub Library
**
** AP-1f: Thin SSI layer that replaces local ufs370 filesystem access.
** Every UFS API call is forwarded to the UFSD STC via IEFSSREQ (SVC 34).
**
** Caller must be APF-authorized before calling any function.
** libufs does not call clib_apf_setup() -- that is the caller's job.
**
** Wire protocol (inline parm area, UFSREQ_MAX_INLINE = 256 bytes):
**   FOPEN   req: [0..3]=mode(unsigned), [4..]=path(NUL-term)
**           rsp: [0..3]=fd(int)
**   FCLOSE  req: [0..3]=fd(int)
**   FREAD   req: [0..3]=fd(int), [4..7]=count(unsigned)
**           rsp: [0..3]=bytes_read(unsigned), [4..]=data
**   FWRITE  req: [0..3]=fd(int), [4..7]=count(unsigned), [8..]=data
**           rsp: [0..3]=bytes_written(unsigned)
**   MKDIR/RMDIR/CHGDIR/REMOVE
**           req: [0..]=path(NUL-terminated)
**   GETCWD  rsp: [0..]=path(NUL-terminated)
**   DIROPEN  req: [0..]=path(NUL-term)
**            rsp: [0..3]=dir_fd(int)
**   DIRREAD  req: [0..3]=dir_fd(int)
**            rsp: [0..3]=ino(0=end), [4..7]=filesize, [8..9]=mode, [12..71]=name
**   DIRCLOSE req: [0..3]=dir_fd(int)
**   SESS_OPEN  rsp: [0..3]=token(unsigned)
**   SESS_CLOSE req: (session token in ufsssob.token)
*/

#include "libufs.h"
#include "ufsd.h"
#include <string.h>
#include <stdlib.h>
#include <iefssobh.h>
#include <iefjssib.h>

/* ============================================================
** libufs_issue
**
** Build SSOB + SSIB, call iefssreq, return R15 (SSRTOK=0 means ok).
** On entry:  ufsssob->func, token, data_len, data[] are filled.
** On return: ufsssob->rc, errno_val, data_len, data[] are filled.
** ============================================================ */
static int
libufs_issue(UFSSSOB *ufsssob)
{
    SSOB  ssob;
    SSIB  ssib;

    memset(&ssib, 0, sizeof(ssib));
    memcpy(ssib.SSIBID,   "SSIB", 4);
    ssib.SSIBLEN = (unsigned short)sizeof(ssib);
    memcpy(ssib.SSIBSSNM, "UFSD", 4);

    memset(&ssob, 0, sizeof(ssob));
    memcpy(ssob.SSOBID, SSOBID_EYE, 4);
    ssob.SSOBLEN  = (unsigned short)sizeof(ssob);
    ssob.SSOBFUNC = (unsigned short)UFSD_SSOBFUNC;
    ssob.SSOBSSIB = &ssib;
    ssob.SSOBRETN = 0;
    ssob.SSOBINDV = ufsssob;

    return iefssreq(&ssob);
}

/* ============================================================
** path_req
**
** Send a function with a single path string parameter.
** Used for MKDIR, RMDIR, CHGDIR, REMOVE.
** Returns UFSD_RC_* (0 = ok, negative = SSI failure).
** ============================================================ */
static int
path_req(unsigned func, unsigned token, const char *path)
{
    UFSSSOB  ufsssob;
    unsigned pathlen;

    if (!path || path[0] == '\0') return UFSD_RC_INVALID;

    pathlen = strlen(path);
    if (pathlen >= (unsigned)UFSREQ_MAX_INLINE)
        pathlen = (unsigned)UFSREQ_MAX_INLINE - 1U;

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = func;
    ufsssob.token    = token;
    ufsssob.data_len = pathlen + 1U;
    memcpy(ufsssob.data, path, pathlen + 1U);

    if (libufs_issue(&ufsssob) != SSRTOK) return -1;
    return ufsssob.rc;
}

/* ============================================================
** Session lifecycle
** ============================================================ */

UFS *
ufsnew(void)
{
    UFSSSOB  ufsssob;
    UFS     *ufs;

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = UFSREQ_SESS_OPEN;
    ufsssob.token    = 0;
    ufsssob.data_len = 0;

    if (libufs_issue(&ufsssob) != SSRTOK)        return NULL;
    if (ufsssob.rc != UFSD_RC_OK)                return NULL;
    if (ufsssob.data_len < (unsigned)sizeof(unsigned)) return NULL;

    ufs = (UFS *)calloc(1, sizeof(UFS));
    if (!ufs) return NULL;

    memcpy(ufs->eye,      "LIBUFSUFS", 8);
    ufs->token        = *(unsigned *)ufsssob.data;
    ufs->create_perm  = 0755U;
    memcpy(ufs->cwd.eye,  "LIBCWD  ", 8);
    ufs->cwd.path[0]  = '/';
    ufs->cwd.path[1]  = '\0';

    return ufs;
}

void
ufsfree(UFS **ufs_pp)
{
    UFSSSOB  ufsssob;
    UFS     *ufs;

    if (!ufs_pp || !*ufs_pp) return;

    ufs = *ufs_pp;
    if (memcmp(ufs->eye, "LIBUFSUFS", 8) != 0) return;

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = UFSREQ_SESS_CLOSE;
    ufsssob.token    = ufs->token;
    ufsssob.data_len = 0;

    libufs_issue(&ufsssob); /* best-effort: ignore R15 */

    ufs->token = 0;
    free(ufs);
    *ufs_pp = NULL;
}

/* ============================================================
** System handle stubs
** ============================================================ */

UFSSYS *
ufs_sys_new(void)
{
    UFSSYS *sys;

    sys = (UFSSYS *)calloc(1, sizeof(UFSSYS));
    if (sys) memcpy(sys->eye, "LIBUFSSY", 8);
    return sys;
}

void
ufs_sys_term(void)
{
    /* no-op: UFSD STC owns the filesystem state */
}

UFSSYS *
ufs_get_sys(UFS *ufs)
{
    if (!ufs) return NULL;
    return ufs->sys;
}

UFSSYS *
ufs_set_sys(UFS *ufs, UFSSYS *sys)
{
    UFSSYS *old;

    if (!ufs) return NULL;
    old      = ufs->sys;
    ufs->sys = sys;
    return old;
}

UFSCWD *
ufs_get_cwd(UFS *ufs)
{
    if (!ufs) return NULL;
    return &ufs->cwd;
}

UFSCWD *
ufs_set_cwd(UFS *ufs, UFSCWD *cwd)
{
    if (!ufs || !cwd) return NULL;
    memcpy(ufs->cwd.path, cwd->path, sizeof(ufs->cwd.path));
    ufs->cwd.path[sizeof(ufs->cwd.path) - 1] = '\0';
    return &ufs->cwd;
}

void *
ufs_get_acee(UFS *ufs)
{
    if (!ufs) return NULL;
    return ufs->acee;
}

void *
ufs_set_acee(UFS *ufs, void *acee)
{
    void *old;

    if (!ufs) return NULL;
    old      = ufs->acee;
    ufs->acee = acee;
    return old;
}

UINT32
ufs_get_create_perm(UFS *ufs)
{
    if (!ufs) return 0755U;
    return ufs->create_perm;
}

UINT32
ufs_set_create_perm(UFS *ufs, UINT32 perm)
{
    UINT32 old;

    if (!ufs) return 0U;
    old              = ufs->create_perm;
    ufs->create_perm = perm;
    return old;
}

/* ============================================================
** Signon / signoff  (Phase 1 stubs)
** ============================================================ */

int
ufs_signon(UFS *ufs, const char *userid, const char *password,
           const char *group)
{
    (void)ufs;
    (void)userid;
    (void)password;
    (void)group;
    return 0;   /* always succeed */
}

void
ufs_signoff(UFS *ufs)
{
    (void)ufs;
}

/* ============================================================
** Directory operations
** ============================================================ */

int
ufs_chgdir(UFS *ufs, const char *path)
{
    int      rc;
    unsigned pathlen;

    if (!ufs) return -1;

    rc = path_req(UFSREQ_CHGDIR, ufs->token, path);
    if (rc == UFSD_RC_OK) {
        /* Mirror the new cwd in the local UFS handle */
        pathlen = strlen(path);
        if (pathlen >= sizeof(ufs->cwd.path))
            pathlen = sizeof(ufs->cwd.path) - 1U;
        memcpy(ufs->cwd.path, path, pathlen);
        ufs->cwd.path[pathlen] = '\0';
    }
    return rc;
}

int
ufs_mkdir(UFS *ufs, const char *path)
{
    if (!ufs) return -1;
    return path_req(UFSREQ_MKDIR, ufs->token, path);
}

int
ufs_rmdir(UFS *ufs, const char *path)
{
    if (!ufs) return -1;
    return path_req(UFSREQ_RMDIR, ufs->token, path);
}

int
ufs_remove(UFS *ufs, const char *path)
{
    if (!ufs) return -1;
    return path_req(UFSREQ_REMOVE, ufs->token, path);
}

/* ============================================================
** Directory listing (AP-1f)
** ============================================================ */

UFSDDESC *
ufs_diropen(UFS *ufs, const char *path, const char *pattern)
{
    UFSSSOB   ufsssob;
    UFSDDESC *ddesc;
    unsigned  pathlen;
    int       fd;

    (void)pattern;  /* Phase 1: no pattern matching */

    if (!ufs || !path) return NULL;

    pathlen = strlen(path);
    if (pathlen >= (unsigned)UFSREQ_MAX_INLINE) return NULL;

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func     = UFSREQ_DIROPEN;
    ufsssob.token    = ufs->token;
    ufsssob.data_len = pathlen + 1U;
    memcpy(ufsssob.data, path, pathlen + 1U);

    if (libufs_issue(&ufsssob) != SSRTOK)    return NULL;
    if (ufsssob.rc != UFSD_RC_OK)            return NULL;
    if (ufsssob.data_len < sizeof(int))      return NULL;

    fd = *(int *)ufsssob.data;
    if (fd < 0) return NULL;

    ddesc = (UFSDDESC *)calloc(1, sizeof(UFSDDESC));
    if (!ddesc) return NULL;

    memcpy(ddesc->eye, "LIBUFSDD", 8);
    ddesc->token = ufs->token;
    ddesc->rec   = (unsigned)fd;
    return ddesc;
}

UFSDLIST *
ufs_dirread(UFSDDESC *ddesc)
{
    UFSSSOB        ufsssob;
    unsigned       ino;
    unsigned short mode;

    if (!ddesc) return NULL;
    if (memcmp(ddesc->eye, "LIBUFSDD", 8) != 0) return NULL;

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func              = UFSREQ_DIRREAD;
    ufsssob.token             = ddesc->token;
    *(unsigned *)ufsssob.data = ddesc->rec;
    ufsssob.data_len          = 4U;

    if (libufs_issue(&ufsssob) != SSRTOK)    return NULL;
    if (ufsssob.rc != UFSD_RC_OK)            return NULL;
    if (ufsssob.data_len < 4U)               return NULL;

    ino = *(unsigned *)ufsssob.data;
    if (ino == 0) return NULL;  /* end of directory */

    memset(&ddesc->result, 0, sizeof(ddesc->result));
    ddesc->result.inode_number = ino;
    ddesc->result.filesize     = *(unsigned *)(ufsssob.data + 4);

    mode = *(unsigned short *)(ufsssob.data + 8);
    ddesc->result.attr[0]  = ((mode & 0xF000U) == 0x4000U) ? 'd' : '-';
    ddesc->result.attr[1]  = 'r';
    ddesc->result.attr[2]  = 'w';
    ddesc->result.attr[3]  = 'x';
    ddesc->result.attr[4]  = 'r';
    ddesc->result.attr[5]  = '-';
    ddesc->result.attr[6]  = 'x';
    ddesc->result.attr[7]  = 'r';
    ddesc->result.attr[8]  = '-';
    ddesc->result.attr[9]  = 'x';
    ddesc->result.attr[10] = '\0';

    memcpy(ddesc->result.name, ufsssob.data + 12, 59);
    ddesc->result.name[59] = '\0';

    if (ufsssob.data_len >= 76U) {
        unsigned mtime_sec;
        ddesc->result.nlink = *(unsigned short *)(ufsssob.data + 10);
        mtime_sec = *(unsigned *)(ufsssob.data + 72);
        __64_from_u32(&ddesc->result.mtime, mtime_sec);
        __64_mul_u32(&ddesc->result.mtime, 1000U, &ddesc->result.mtime);
    }

    return &ddesc->result;
}

void
ufs_dirclose(UFSDDESC **pddesc)
{
    UFSSSOB   ufsssob;
    UFSDDESC *ddesc;

    if (!pddesc || !*pddesc) return;

    ddesc = *pddesc;
    if (memcmp(ddesc->eye, "LIBUFSDD", 8) != 0) return;

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func              = UFSREQ_DIRCLOSE;
    ufsssob.token             = ddesc->token;
    *(unsigned *)ufsssob.data = ddesc->rec;
    ufsssob.data_len          = 4U;

    libufs_issue(&ufsssob);  /* best-effort */

    free(ddesc);
    *pddesc = NULL;
}

/* ============================================================
** File open / close / sync
** ============================================================ */

UFSFILE *
ufs_fopen(UFS *ufs, const char *path, const char *mode_str)
{
    UFSSSOB  ufsssob;
    UFSFILE *fp;
    unsigned mode;
    unsigned pathlen;

    if (!ufs || !path || !mode_str) return NULL;

    /* Parse mode: "r" or "rb" = read, anything else = write */
    if (mode_str[0] == 'r' || mode_str[0] == 'R')
        mode = UFSD_OPEN_READ;
    else
        mode = UFSD_OPEN_WRITE;

    pathlen = strlen(path);
    /* 4 bytes mode + path + NUL must fit in data[] */
    if (4U + pathlen + 1U > (unsigned)UFSREQ_MAX_INLINE) return NULL;

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func             = UFSREQ_FOPEN;
    ufsssob.token            = ufs->token;
    *(unsigned *)ufsssob.data = mode;
    memcpy(ufsssob.data + 4, path, pathlen + 1U);
    ufsssob.data_len         = 4U + pathlen + 1U;

    if (libufs_issue(&ufsssob) != SSRTOK)        return NULL;
    if (ufsssob.rc != UFSD_RC_OK)                return NULL;
    if (ufsssob.data_len < (unsigned)sizeof(int)) return NULL;

    fp = (UFSFILE *)calloc(1, sizeof(UFSFILE));
    if (!fp) return NULL;

    memcpy(fp->eye, "LIBUFSFP", 8);
    fp->token = ufs->token;
    fp->fd    = *(int *)ufsssob.data;
    fp->flags = 0;
    fp->error = 0;

    return fp;
}

void
ufs_fclose(UFSFILE **fpp)
{
    UFSSSOB  ufsssob;
    UFSFILE *fp;

    if (!fpp || !*fpp) return;

    fp = *fpp;
    if (memcmp(fp->eye, "LIBUFSFP", 8) != 0) return;

    memset(&ufsssob, 0, sizeof(ufsssob));
    memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
    ufsssob.func              = UFSREQ_FCLOSE;
    ufsssob.token             = fp->token;
    *(unsigned *)ufsssob.data = (unsigned)fp->fd;
    ufsssob.data_len          = 4U;

    libufs_issue(&ufsssob); /* best-effort: ignore R15 */

    fp->fd = -1;
    free(fp);
    *fpp = NULL;
}

void
ufs_fsync(UFSFILE *file)
{
    (void)file;
    /* no-op in Phase 1 */
}

void
ufs_sync(UFS *ufs)
{
    (void)ufs;
    /* no-op in Phase 1 */
}

/* ============================================================
** Bulk read / write
**
** FREAD  max inline data: 256 - 4  = 252 bytes per SSI call
** FWRITE max inline data: 256 - 8  = 248 bytes per SSI call
** Chunked automatically for larger transfers.
** ============================================================ */

#define LIBUFS_READ_CHUNK  252U
#define LIBUFS_WRITE_CHUNK 248U

UINT32
ufs_fread(void *ptr, UINT32 size, UINT32 nitems, UFSFILE *fp)
{
    UFSSSOB  ufsssob;
    char    *dst;
    unsigned total;
    unsigned done;
    unsigned want;
    unsigned got;

    if (!ptr || !fp || fp->fd < 0)           return 0;
    if (fp->flags & (LIBUFS_F_ERR | LIBUFS_F_EOF)) return 0;

    total = size * nitems;
    if (total == 0) return 0;

    dst  = (char *)ptr;
    done = 0;

    while (done < total) {
        want = total - done;
        if (want > LIBUFS_READ_CHUNK) want = LIBUFS_READ_CHUNK;

        memset(&ufsssob, 0, sizeof(ufsssob));
        memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
        ufsssob.func                    = UFSREQ_FREAD;
        ufsssob.token                   = fp->token;
        *(unsigned *)ufsssob.data       = (unsigned)fp->fd;
        *(unsigned *)(ufsssob.data + 4) = want;
        ufsssob.data_len = 8U;

        if (libufs_issue(&ufsssob) != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
            fp->flags |= LIBUFS_F_ERR;
            fp->error   = ufsssob.rc;
            break;
        }

        got = *(unsigned *)ufsssob.data;
        if (got == 0) {
            fp->flags |= LIBUFS_F_EOF;
            break;
        }
        if (got > want) got = want; /* clamp: should never happen */

        memcpy(dst + done, ufsssob.data + 4, got);
        done += got;

        if (got < want) {
            /* Partial read: EOF reached mid-transfer */
            fp->flags |= LIBUFS_F_EOF;
            break;
        }
    }

    if (size == 0) return 0;
    return done / size;
}

UINT32
ufs_fwrite(void *ptr, UINT32 size, UINT32 nitems, UFSFILE *fp)
{
    UFSSSOB     ufsssob;
    const char *src;
    unsigned    total;
    unsigned    done;
    unsigned    want;
    unsigned    written;

    if (!ptr || !fp || fp->fd < 0)  return 0;
    if (fp->flags & LIBUFS_F_ERR)   return 0;

    total = size * nitems;
    if (total == 0) return 0;

    src  = (const char *)ptr;
    done = 0;

    while (done < total) {
        want = total - done;
        if (want > LIBUFS_WRITE_CHUNK) want = LIBUFS_WRITE_CHUNK;

        memset(&ufsssob, 0, sizeof(ufsssob));
        memcpy(ufsssob.eye, UFSSSOB_EYE, 4);
        ufsssob.func                    = UFSREQ_FWRITE;
        ufsssob.token                   = fp->token;
        *(unsigned *)ufsssob.data       = (unsigned)fp->fd;
        *(unsigned *)(ufsssob.data + 4) = want;
        memcpy(ufsssob.data + 8, src + done, want);
        ufsssob.data_len = 8U + want;

        if (libufs_issue(&ufsssob) != SSRTOK || ufsssob.rc != UFSD_RC_OK) {
            fp->flags |= LIBUFS_F_ERR;
            fp->error   = ufsssob.rc;
            break;
        }

        written = *(unsigned *)ufsssob.data;
        done += written;

        if (written < want) break; /* short write */
    }

    if (size == 0) return 0;
    return done / size;
}

/* ============================================================
** Character / line I/O  (built on ufs_fread / ufs_fwrite)
** ============================================================ */

INT32
ufs_fgetc(UFSFILE *file)
{
    unsigned char c;

    if (ufs_fread(&c, 1, 1, file) == 1)
        return (INT32)(unsigned)c;
    return UFS_EOF;
}

INT32
ufs_fputc(INT32 c, UFSFILE *file)
{
    unsigned char uc;

    uc = (unsigned char)c;
    if (ufs_fwrite(&uc, 1, 1, file) == 1)
        return (INT32)(unsigned)uc;
    return UFS_EOF;
}

INT32
ufs_fputs(const char *str, UFSFILE *file)
{
    unsigned len;

    if (!str || !file) return UFS_EOF;
    len = strlen(str);
    if (ufs_fwrite((void *)str, 1, len, file) == len)
        return (INT32)len;
    return UFS_EOF;
}

char *
ufs_fgets(char *str, int num, UFSFILE *fp)
{
    int   i;
    INT32 c;

    if (!str || num <= 0 || !fp) return NULL;

    for (i = 0; i < num - 1; i++) {
        c = ufs_fgetc(fp);
        if (c == UFS_EOF) {
            if (i == 0) return NULL; /* nothing read */
            break;
        }
        str[i] = (char)c;
        if (c == '\n') { i++; break; }
    }
    str[i] = '\0';
    return str;
}

/* ============================================================
** Seek / status
** ============================================================ */

INT32
ufs_fseek(UFSFILE *fp, INT32 offset, INT32 whence)
{
    /* Deferred to post-AP-1f */
    (void)fp;
    (void)offset;
    (void)whence;
    return -1;
}

INT32
ufs_feof(UFSFILE *file)
{
    if (!file) return 1;
    return (file->flags & LIBUFS_F_EOF) ? 1 : 0;
}

INT32
ufs_ferror(UFSFILE *file)
{
    if (!file) return 1;
    return (file->flags & LIBUFS_F_ERR) ? file->error : 0;
}

void
ufs_clearerr(UFSFILE *file)
{
    if (!file) return;
    file->flags &= ~(LIBUFS_F_EOF | LIBUFS_F_ERR);
    file->error  = 0;
}
