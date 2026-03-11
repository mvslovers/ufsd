/*
** UFSD.H - UFSD Filesystem Server Control Blocks
**
** AP-1a: UFSD_STC    local STC state (STC skeleton, no CSA)
** AP-1b: UFSD_ANCHOR, UFSREQ, UFSBUF   CSA structures
** AP-1b: UFSD_TRACE  diagnostic trace ring buffer
** AP-1d: UFSD_SESSION, UFSD_FD         session table (STC-local)
** AP-1e: UFSD_GFILE  global open file table (STC-local)
*/

#ifndef UFSD_H
#define UFSD_H

#include <clibecb.h>
#include <clibcib.h>

/* ============================================================
** Common status flags
** ============================================================ */

#define UFSD_ACTIVE    0x80000000U
#define UFSD_QUIESCE   0x40000000U

/* ============================================================
** AP-1a: Local STC State
**
** Stack-allocated in main(). No CSA. Used for AP-1a only.
** Replaced by UFSD_ANCHOR in CSA from AP-1b onward.
** ============================================================ */

typedef struct ufsd_stc     UFSD_STC;

struct ufsd_stc {
    char        eye[8];     /* "**UFSD**"                       */
    unsigned    flags;      /* UFSD_ACTIVE / UFSD_QUIESCE       */
    ECB         wait_ecb;   /* reserved; AP-1b: server_ecb      */
};

/* ============================================================
** AP-1b: CSA Anchor Block  (~512 bytes, GETMAIN SP=241)
**
** Located via SSCT->ssctsuse. Eye catcher "UFSDANCR".
** ============================================================ */

typedef struct ufsd_anchor  UFSD_ANCHOR;
typedef struct ufsreq       UFSREQ;
typedef struct ufsbuf       UFSBUF;
typedef struct ufsd_session UFSD_SESSION;
typedef struct ufsd_fd      UFSD_FD;
typedef struct ufsd_gfile   UFSD_GFILE;
typedef struct ufsd_trace   UFSD_TRACE;

/* Anchor flags (same bit positions as STC flags for consistency) */
#define UFSD_ANCHOR_ACTIVE   0x80000000U
#define UFSD_ANCHOR_QUIESCE  0x40000000U

struct ufsd_anchor {
    char            eye[8];         /* "UFSDANCR"                  */
    unsigned        version;        /* anchor version              */
    unsigned        flags;          /* UFSD_ANCHOR_*               */

    ECB             server_ecb;     /* master ECB: STC WAIT target */

    /* Request Queue (CS lock-free, single producer / single consumer) */
    UFSREQ         *req_head;       /* dequeue from head           */
    UFSREQ         *req_tail;       /* enqueue to tail (CS)        */

    /* Free Request Pool (free chain, CS-protected) */
    UFSREQ         *free_head;
    unsigned        free_count;
    unsigned        total_reqs;

    /* 4K Buffer Pool (free chain) */
    UFSBUF         *buf_free;
    unsigned        buf_count;
    unsigned        buf_total;

    /* Session + Global File Tables (pointers, arrays in STC space) */
    UFSD_SESSION   *sessions;
    unsigned        max_sessions;
    UFSD_GFILE     *gfiles;
    unsigned        max_gfiles;

    /* Trace Ring Buffer */
    UFSD_TRACE     *trace_buf;      /* pointer into CSA            */
    unsigned        trace_next;     /* next write index (wraps)    */
    unsigned        trace_size;     /* ring capacity in entries    */

    /* Statistics */
    unsigned        stat_requests;
    unsigned        stat_errors;
    unsigned        stat_posts_saved; /* conditional POST savings  */
};

/* ============================================================
** AP-1b: Request Block  (~304 bytes, pre-allocated in CSA pool)
**
** Allocated by SSI router in client address space from the
** free pool in the CSA anchor. Chain pointer (next) used for
** both the free pool and the active request queue.
** ============================================================ */

/* Function codes */
#define UFSREQ_MIN          0x0001U
#define UFSREQ_PING         0x0001U  /* AP-1c: round-trip test       */
#define UFSREQ_SESS_OPEN    0x0010U  /* AP-1d: open session          */
#define UFSREQ_SESS_CLOSE   0x0011U  /* AP-1d: close session         */
#define UFSREQ_FOPEN        0x0020U  /* AP-1e: open file             */
#define UFSREQ_FCLOSE       0x0021U  /* AP-1e: close file            */
#define UFSREQ_FREAD        0x0022U  /* AP-1e: read file             */
#define UFSREQ_FWRITE       0x0023U  /* AP-1e: write file            */
#define UFSREQ_FSEEK        0x0024U  /* AP-1e: seek file             */
#define UFSREQ_MKDIR        0x0030U  /* AP-1e: make directory        */
#define UFSREQ_CHGDIR       0x0031U  /* AP-1e: change directory      */
#define UFSREQ_RMDIR        0x0032U  /* AP-1e: remove directory      */
#define UFSREQ_REMOVE       0x0033U  /* AP-1e: remove file           */
#define UFSREQ_DIROPEN      0x0040U  /* AP-1e: open directory        */
#define UFSREQ_DIRREAD      0x0041U  /* AP-1e: read directory entry  */
#define UFSREQ_DIRCLOSE     0x0042U  /* AP-1e: close directory       */
#define UFSREQ_MAX          0x00FFU

/* Return codes */
#define UFSD_RC_OK          0   /* success                          */
#define UFSD_RC_NOREQ       4   /* no free request block available  */
#define UFSD_RC_CORRUPT     8   /* corrupt request block (eye fail) */
#define UFSD_RC_BADFUNC     12  /* invalid function code            */
#define UFSD_RC_BADSESS     16  /* invalid session token            */
#define UFSD_RC_INVALID     20  /* validation failed                */
#define UFSD_RC_NOTIMPL     24  /* not yet implemented              */

#define UFSREQ_MAX_INLINE   256 /* max bytes in data[] field        */

struct ufsreq {
    char            eye[8];         /* "UFSREQ__"                  */
    UFSREQ         *next;           /* free pool or queue chain    */
    ECB             client_ecb;     /* posted by server on done    */
    unsigned        func;           /* function code (UFSREQ_*)   */
    unsigned        session_token;  /* client session ID           */
    unsigned        client_asid;    /* caller ASID (for cleanup)   */
    int             rc;             /* return code                 */
    int             errno_val;      /* errno                       */
    unsigned        data_len;       /* bytes used in data[]        */
    UFSBUF         *buf;            /* 4K pool buffer (or NULL)    */
    char            data[UFSREQ_MAX_INLINE]; /* inline parm/result */
}; /* ~304 bytes */

/* ============================================================
** AP-1b: 4K Data Buffer  (pre-allocated in CSA pool)
**
** Used for fread/fwrite > 256 bytes. Chunked for > 4K.
** ============================================================ */

struct ufsbuf {
    UFSBUF         *next;           /* free chain                  */
    char            data[4096];
};

/* ============================================================
** AP-1b: Trace Ring Buffer Entry  (16 bytes)
**
** Ring in CSA: 256 entries = 4K (standard), 1024 = 16K (debug).
** Wrap-around; oldest entries overwritten silently.
** ============================================================ */

/* Trace event codes */
#define UFSD_T_REQUEST      0x0001U  /* request received            */
#define UFSD_T_COMPLETE     0x0002U  /* request completed           */
#define UFSD_T_SESS_OPEN    0x0010U  /* session opened              */
#define UFSD_T_SESS_CLOSE   0x0011U  /* session closed              */
#define UFSD_T_SESS_ABEND   0x0012U  /* session cleanup after abend */
#define UFSD_T_MODIFY       0x0020U  /* operator MODIFY command     */
#define UFSD_T_CORRUPT      0x00F0U  /* corrupt request block       */
#define UFSD_T_BADFUNC      0x00F1U  /* invalid function code       */
#define UFSD_T_BADSESS      0x00F2U  /* invalid session token       */

struct ufsd_trace {
    unsigned        timestamp;      /* STCK low word (TOD)         */
    unsigned short  func;           /* event code (UFSD_T_*)       */
    unsigned short  rc;             /* return code                 */
    unsigned        session_token;  /* session that caused event   */
    unsigned        asid;           /* address space ID            */
}; /* exactly 16 bytes */

/* ============================================================
** AP-1d: Session Table  (STC address space, not CSA)
**
** Max 64 concurrent sessions. Token = index | validation byte.
** ============================================================ */

#define UFSD_MAX_FD         64   /* max open files per session     */
#define UFSD_MAX_SESSIONS   64   /* max concurrent sessions        */

/* Per-session file descriptor flags */
#define UFSD_FD_UNUSED      0xFFFFFFFFU  /* slot is free            */
#define UFSD_FD_READ        0x80000000U
#define UFSD_FD_WRITE       0x40000000U
#define UFSD_FD_APPEND      0x20000000U

struct ufsd_fd {
    unsigned        gfile_idx;      /* index into global file table */
    unsigned        flags;          /* UFSD_FD_*                    */
};

/* Session flags */
#define UFSD_SESS_ACTIVE    0x80000000U
#define UFSD_SESS_CLEANUP   0x40000000U  /* being cleaned up (abend) */

struct ufsd_session {
    char            eye[8];         /* "UFSDSESS"                  */
    unsigned        token;          /* session token               */
    unsigned        client_asid;    /* client address space        */
    unsigned        flags;          /* UFSD_SESS_*                 */
    void           *ufs;            /* server-side UFS handle      */
    UFSD_FD         fd_table[UFSD_MAX_FD];
};

/* ============================================================
** AP-1e: Global Open File Table  (STC address space, not CSA)
**
** Max 256 entries. Refcount tracks sessions referencing a file.
** In Phase 1 refcount is always 1 (no fork, no shared fds).
** ============================================================ */

#define UFSD_MAX_GFILES     256  /* max global open files          */

/* Global file flags */
#define UFSD_GF_USED        0x80000000U

struct ufsd_gfile {
    char            eye[8];         /* "UFSDGFIL"                  */
    unsigned        flags;          /* UFSD_GF_*                   */
    void           *minode;         /* memory inode (UFSMIN *)     */
    void           *vdisk;          /* virtual disk (UFSVDISK *)   */
    unsigned        position;       /* current file offset         */
    unsigned        open_mode;      /* mode flags from fopen       */
    unsigned        refcount;       /* sessions referencing this   */
};

/* ============================================================
** Function Prototypes
** ============================================================ */

/* ufsd#cmd.c (AP-1a) */
int  ufsd_process_cib(UFSD_STC *ufsd, CIB *cib);

/* ufsd.c (AP-1a) */
void ufsd_shutdown(UFSD_STC *ufsd);

#endif /* UFSD_H */
