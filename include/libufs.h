/* LIBUFS.H - libufs UFS API Compatibility Header
**
** AP-1f: Drop-in replacement for ufs370's ufs.h / ufs/sys.h.
** Include this header (instead of <ufs/sys.h>) when using the
** UFSD filesystem server.  All operations are forwarded via
** IEFSSREQ (SVC 34) to the UFSD STC.
**
** Types defined here are intentionally smaller than their ufs370
** counterparts: only the fields that libufs.c needs internally
** are present.  Callers must treat UFS * and UFSFILE * as opaque.
*/

#ifndef LIBUFS_H
#define LIBUFS_H

#include "time64.h"     /* mtime64_t, mlocaltime64() */
#include "clib64.h"     /* __64_from_u32, __64_mul_u32 */

/* ============================================================
** Atomic types  (mirror ufs/types.h)
** ============================================================ */

#ifndef TYPE_BYTE
#define TYPE_BYTE
typedef unsigned char   BYTE;
#endif

#ifndef TYPE_INT32
#define TYPE_INT32
typedef int             INT32;
#endif

#ifndef TYPE_UINT32
#define TYPE_UINT32
typedef unsigned int    UINT32;
#endif

#ifndef TYPE_UINT16
#define TYPE_UINT16
typedef unsigned short  UINT16;
#endif

/* UFS_PATH_MAX: maximum path length */
#define UFS_PATH_MAX    256

/* UFS_EOF: returned on end-of-file by character I/O functions */
#define UFS_EOF     (-1)

/* ufs_fseek() whence constants */
#define UFS_SEEK_SET    0
#define UFS_SEEK_CUR    1
#define UFS_SEEK_END    2

/* ============================================================
** UFSCWD -- current working directory
** Returned by ufs_get_cwd().  The path[] field holds the
** current directory string; all other fields are stubs.
** ============================================================ */

typedef struct libufs_cwd  UFSCWD;
struct libufs_cwd {
    char     eye[8];        /* "LIBCWD  "                    */
    char     path[256];     /* current directory path string */
};

/* ============================================================
** UFSSYS -- filesystem system handle (stub)
** Not used internally by libufs; present for API compatibility
** with code that calls ufs_sys_new() / ufs_get_sys().
** ============================================================ */

typedef struct libufs_sys  UFSSYS;
struct libufs_sys {
    char     eye[8];        /* "LIBUFSSY"                    */
};

/* ============================================================
** UFS -- per-session handle
**
** Allocated by ufsnew() via UFSREQ_SESS_OPEN.
** Freed by ufsfree() via UFSREQ_SESS_CLOSE.
** token holds the UFSD session token returned by the STC.
** ============================================================ */

/* UFSFILE flags */
#define LIBUFS_F_EOF    0x80000000U  /* end-of-file reached */
#define LIBUFS_F_ERR    0x40000000U  /* I/O error occurred  */

typedef struct libufs_ufs  UFS;
struct libufs_ufs {
    char     eye[8];        /* "LIBUFSUFS"                   */
    unsigned token;         /* UFSD session token            */
    unsigned flags;         /* reserved                      */
    unsigned create_perm;   /* default 0755                  */
    UFSCWD   cwd;           /* local copy of current dir     */
    UFSSYS  *sys;           /* stub system pointer           */
    void    *acee;          /* ACEE pointer (Phase 1 unused) */
};

/* ============================================================
** UFSFILE -- per-file handle
**
** Allocated by ufs_fopen() via UFSREQ_FOPEN.
** Freed by ufs_fclose() via UFSREQ_FCLOSE.
** fd holds the UFSD per-session file descriptor index.
** ============================================================ */

/* Read-ahead buffer size for ufs_fgetc / ufs_fgets.
** When the 4K CSA pool path is available, one refill transfers up to 4096
** bytes in a single SSI round-trip.  Falls back to 252 bytes (inline) if
** the pool is exhausted -- ufs_fread handles the difference transparently. */
#define LIBUFS_GETC_BUFSZ  4096

typedef struct libufs_file  UFSFILE;
struct libufs_file {
    char     eye[8];        /* "LIBUFSFP"                    */
    unsigned token;         /* session token (from UFS)      */
    int      fd;            /* UFSD file descriptor index    */
    unsigned flags;         /* LIBUFS_F_*                    */
    int      error;         /* last error code (UFSD_RC_*)   */
    unsigned rbuf_pos;      /* next byte to consume in rbuf  */
    unsigned rbuf_len;      /* valid bytes in rbuf           */
    char     rbuf[LIBUFS_GETC_BUFSZ]; /* read-ahead buffer (4K or inline fallback) */
};

/* ============================================================
** UFSDLIST -- directory entry returned by ufs_dirread
** ============================================================ */

typedef struct libufs_dirlist  UFSDLIST;
struct libufs_dirlist {
    unsigned       inode_number;
    unsigned       filesize;
    char           name[60];      /* filename + NUL                */
    char           attr[11];      /* "drwxrwxrwx" + NUL            */
    char           owner[9];      /* owner name + NUL              */
    char           group[9];      /* group name + NUL              */
    unsigned short nlink;         /* hard link count               */
    unsigned short unused;
    mtime64_t      mtime;         /* modification time (milliseconds, mtime64_t) */
};

/* ============================================================
** UFSDDESC -- directory handle
**
** AP-1f: Allocated by ufs_diropen(), freed by ufs_dirclose().
** The result field is reused by each ufs_dirread() call.
** ============================================================ */

typedef struct libufs_dirdesc  UFSDDESC;
struct libufs_dirdesc {
    char     eye[8];        /* "LIBUFSDD"                    */
    unsigned token;         /* session token                 */
    unsigned rec;           /* directory fd (from DIROPEN)   */
    UFSDLIST *entries;      /* sorted entry array (heap)     */
    unsigned nentries;      /* number of entries in array    */
    unsigned cur;           /* next entry index for dirread  */
    UFSDLIST result;        /* reused by ufs_dirread         */
};

/* ============================================================
** Function declarations
**
** asm() aliases match the MVS NCALIB member names exported by
** ufs370.  A program that was linked against ufs370 can be
** re-linked against libufs with no source changes.
** ============================================================ */

/* Session lifecycle */
UFS    *ufsnew(void);
void    ufsfree(UFS **ufs);

/* System handle stubs */
UFSSYS *ufs_sys_new(void)                                       asm("UFSSYNEW");
void    ufs_sys_term(void)                                      asm("UFSSYTRM");

/* Handle accessors */
UFSSYS *ufs_get_sys(UFS *ufs)                                   asm("UFS#GSYS");
UFSSYS *ufs_set_sys(UFS *ufs, UFSSYS *sys)                      asm("UFS#SSYS");
UFSCWD *ufs_get_cwd(UFS *ufs)                                   asm("UFS#GCWD");
UFSCWD *ufs_set_cwd(UFS *ufs, UFSCWD *cwd)                      asm("UFS#SCWD");
void   *ufs_get_acee(UFS *ufs)                                  asm("UFS#GACE");
void   *ufs_set_acee(UFS *ufs, void *acee)                      asm("UFS#SACE");
UINT32  ufs_get_create_perm(UFS *ufs)                           asm("UFS#GCRE");
UINT32  ufs_set_create_perm(UFS *ufs, UINT32 perm)              asm("UFS#SCRE");

/* Signon / signoff  (Phase 1 stubs -- no RACF integration) */
int     ufs_signon(UFS *ufs, const char *userid,
                   const char *password,
                   const char *group)                           asm("UFS#SON");
void    ufs_signoff(UFS *ufs)                                   asm("UFS#SOFF");

/* Directory operations */
int      ufs_chgdir(UFS *ufs, const char *path)                 asm("UFS#CD");
int      ufs_mkdir(UFS *ufs, const char *path)                  asm("UFS#MD");
int      ufs_rmdir(UFS *ufs, const char *path)                  asm("UFS#RD");
int      ufs_remove(UFS *ufs, const char *path)                 asm("UFS#REM");

/* Directory listing  (AP-1f) */
UFSDDESC *ufs_diropen(UFS *ufs, const char *path,
                      const char *pattern)                      asm("UFS#DOPN");
UFSDLIST *ufs_dirread(UFSDDESC *ddesc)                          asm("UFS#DRD");
void      ufs_dirclose(UFSDDESC **pddesc)                       asm("UFS#DCLS");

/* File open / close / sync */
UFSFILE *ufs_fopen(UFS *ufs, const char *path,
                   const char *mode)                            asm("UFS#FOPN");
void     ufs_fclose(UFSFILE **file)                             asm("UFS#FCLS");
void     ufs_fsync(UFSFILE *file)                               asm("UFS#FSYN");
void     ufs_sync(UFS *ufs)                                     asm("UFS#SYNC");

/* Bulk I/O  (chunked for > 248 bytes) */
UINT32   ufs_fread(void *ptr, UINT32 size, UINT32 nitems,
                   UFSFILE *fp)                                 asm("UFS#FRD");
UINT32   ufs_fwrite(void *ptr, UINT32 size, UINT32 nitems,
                    UFSFILE *fp)                                asm("UFS#FWT");

/* Character / line I/O  (built on ufs_fread / ufs_fwrite) */
char    *ufs_fgets(char *str, int num, UFSFILE *fp)             asm("UFS#FGTS");
INT32    ufs_fputc(INT32 c, UFSFILE *file)                      asm("UFS#FPTC");
INT32    ufs_fputs(const char *str, UFSFILE *file)              asm("UFS#FPTS");
INT32    ufs_fgetc(UFSFILE *file)                               asm("UFS#FGTC");

/* Seek / status */
INT32    ufs_fseek(UFSFILE *fp, INT32 offset, INT32 whence)     asm("UFS#FSEE");
INT32    ufs_feof(UFSFILE *file)                                asm("UFS#FEOF");
INT32    ufs_ferror(UFSFILE *file)                              asm("UFS#FERR");
void     ufs_clearerr(UFSFILE *file)                            asm("UFS#CERR");

#endif /* LIBUFS_H */
