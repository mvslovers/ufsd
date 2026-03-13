/*
** UFSD.H - UFSD Filesystem Server Control Blocks
**
** AP-1a: UFSD_STC    local STC state (STC skeleton, no CSA)
** AP-1b: UFSD_ANCHOR, UFSREQ, UFSBUF   CSA structures
** AP-1b: UFSD_TRACE  diagnostic trace ring buffer
** AP-1d: UFSD_SESSION, UFSD_FD         session table (STC-local)
** AP-1d: UFSD_DISK, UFSD_UFS           disk handles + per-session UFS state
** AP-1e: UFSD_SB, UFSD_DINODE, UFSD_DIRENT   on-disk structures
** AP-1e: UFSD_GFILE  global open file table (STC-local)
*/

#ifndef UFSD_H
#define UFSD_H

#include <clibecb.h>
#include <clibcib.h>
#include <clibssct.h>
#include <clibssvt.h>

/* ============================================================
** Common status flags
** ============================================================ */

#define UFSD_ACTIVE    0x80000000U
#define UFSD_QUIESCE   0x40000000U

/* ============================================================
** Forward declarations for all control blocks
** ============================================================ */

typedef struct ufsd_stc     UFSD_STC;
typedef struct ufsd_anchor  UFSD_ANCHOR;
typedef struct ufsreq       UFSREQ;
typedef struct ufsbuf       UFSBUF;
typedef struct ufsd_session UFSD_SESSION;
typedef struct ufsd_fd      UFSD_FD;
typedef struct ufsd_gfile   UFSD_GFILE;
typedef struct ufsd_trace   UFSD_TRACE;
typedef struct ufsd_disk    UFSD_DISK;
typedef struct ufsd_ufs     UFSD_UFS;
typedef struct ufsd_sb      UFSD_SB;
typedef struct ufsd_dinode  UFSD_DINODE;
typedef struct ufsd_dirent  UFSD_DIRENT;

/* ============================================================
** AP-1e: On-disk UFS constants
**
** Derived from ufs370/include/ufs/disk.h and inode.h.
** ============================================================ */

#define UFSD_ROOT_INO           2U   /* root dir inode (inode 1 = BALBLK)     */
#define UFSD_ILIST_SECTOR       2U   /* ilist starts at disk sector 2          */
#define UFSD_INODE_SIZE         128U /* on-disk inode size in bytes            */
#define UFSD_DIRENT_SIZE        64U  /* on-disk directory entry size in bytes  */
#define UFSD_NAME_MAX           59U  /* max filename chars (excl. NUL)         */
#define UFSD_NADDR              19U  /* total address slots in inode           */
#define UFSD_NADDR_DIRECT       16U  /* direct data block slots (addr[0..15])  */

#define UFSD_SB_MAX_FREEBLOCK   51U  /* free block cache size in superblock    */
#define UFSD_SB_MAX_FREEINODE   64U  /* free inode cache size in superblock    */

/* Inode type flags (V7 Unix, matches ufs370 disk.h) */
#define UFSD_IFMT   0xF000U  /* file type mask  */
#define UFSD_IFDIR  0x4000U  /* directory       */
#define UFSD_IFREG  0x8000U  /* regular file    */

/* File open mode flags (AP-1e) */
#define UFSD_OPEN_READ   0x01U  /* open for reading                           */
#define UFSD_OPEN_WRITE  0x02U  /* open for writing (create + truncate)       */

/* ============================================================
** AP-1e: On-disk Superblock  (512 bytes at sector 1, offset 0)
**
** Layout exactly matches struct ufs_superblock in
** ufs370/include/ufs/disk.h (time_t = unsigned on MVS 3.8j).
** ============================================================ */

struct ufsd_sb {
    unsigned        datablock_start;        /* 000 first data sector            */
    unsigned        volume_size;            /* 004 total disk sectors           */
    unsigned char   lock_freeblock;         /* 008 freeblock lock byte          */
    unsigned char   lock_freeinode;         /* 009 freeinode lock byte          */
    unsigned char   modified;               /* 00A modified flag                */
    unsigned char   rdonly;                 /* 00B read-only flag               */
    unsigned        update_time;            /* 00C update timestamp (time_t=4)  */
    unsigned        total_freeblock;        /* 010 total free block count       */
    unsigned        total_freeinode;        /* 014 total free inode count       */
    unsigned        create_time;            /* 018 create timestamp (time_t=4)  */
    unsigned        nfreeblock;             /* 01C entries in freeblock[]       */
    unsigned        freeblock[51];          /* 020 free block cache  (204 bytes)*/
    unsigned        nfreeinode;             /* 0EC entries in freeinode[]       */
    unsigned        freeinode[64];          /* 0F0 free inode cache  (256 bytes)*/
    unsigned        inodes_per_block;       /* 1F0 inodes per physical block    */
    unsigned        blksize_shift;          /* 1F4 block size in shift bits     */
    unsigned        ilist_sector;           /* 1F8 sector where ilist begins    */
    unsigned        unused3;               /* 1FC reserved                     */
};                                          /* 200 = 512 bytes                  */

/* ============================================================
** AP-1e: On-disk Inode  (128 bytes per inode)
**
** Layout exactly matches struct ufs_dinode in
** ufs370/include/ufs/inode.h (UFSTIMEV = two UINT32 = 8 bytes).
** ============================================================ */

struct ufsd_dinode {
    unsigned short  mode;        /* 000 file type + permissions               */
    unsigned short  nlink;       /* 002 link count                            */
    unsigned        filesize;    /* 004 file size in bytes                    */
    unsigned        ctime_sec;   /* 008 creation time (seconds)               */
    unsigned        ctime_usec;  /* 00C creation time (microseconds)          */
    unsigned        mtime_sec;   /* 010 modified time (seconds)               */
    unsigned        mtime_usec;  /* 014 modified time (microseconds)          */
    unsigned        atime_sec;   /* 018 access time (seconds)                 */
    unsigned        atime_usec;  /* 01C access time (microseconds)            */
    char            owner[9];    /* 020 owner user id + NUL                   */
    char            group[9];    /* 029 group name + NUL                      */
    unsigned short  codepage;    /* 032 code page (0 = default)               */
    unsigned        addr[19];    /* 034 block address list (direct+indirect)  */
};                               /* 080 = 128 bytes                           */

/* ============================================================
** AP-1e: On-disk Directory Entry  (64 bytes per entry)
**
** Layout exactly matches struct ufs_dirent in
** ufs370/include/ufs/dir.h (UFS_NAME_MAX=59, name[60]).
** ============================================================ */

struct ufsd_dirent {
    unsigned        ino;         /* 00 inode number (0 = free entry)         */
    char            name[60];    /* 04 filename: max 59 chars + NUL          */
};                               /* 40 = 64 bytes                            */

/* ============================================================
** AP-1d: Physical Disk Handle  (STC heap)
**
** One per UFSDISK0-9 DD card.  Opened via BDAM at STC startup.
** Closed and freed at STC shutdown.
**
** AP-1e: sb field added (read from disk sector 1 at open time).
** AP-1f: mountpath added (set by ufsd_ufs_init and ufsd_disk_mount).
** ============================================================ */

/* Disk flags */
#define UFSD_DISK_OPEN    0x80000000U
#define UFSD_DISK_RDONLY  0x40000000U  /* DISP=SHR allocation      */
#define UFSD_DISK_ROOT    0x20000000U  /* first disk = root (/)    */

#define UFSD_MAX_DISKS    10           /* UFSDISK0 ... UFSDISK9 */

struct ufsd_disk {
    char           ddname[9];       /* DD name + NUL ("UFSDISK0")    */
    char           dsn[45];         /* dataset name from JFCB + NUL  */
    void          *dcb;             /* BDAM DCB (opaque: see osio.h) */
    unsigned       flags;           /* UFSD_DISK_*                   */
    unsigned short blksize;         /* physical block size from DCB  */
    unsigned short pad;             /* alignment                     */
    UFSD_SB        sb;              /* superblock (read at open)     */
    char           mountpath[128];  /* mount point path (AP-1f)      */
};

/* ============================================================
** AP-1d: Per-Session UFS Handle  (STC heap)
**
** Allocated on SESS_OPEN, freed on SESS_CLOSE.
** AP-1e: cwd_ino and disk_idx added.
** ============================================================ */

struct ufsd_ufs {
    char        eye[8];             /* "UFSD_UFS"                    */
    unsigned    flags;
    char        cwd[256];           /* current working directory path */
    unsigned    cwd_ino;            /* inode number of cwd           */
    int         disk_idx;           /* index into stc->disks[] (0)   */
};

struct ufsd_stc {
    char            eye[8];         /* "**UFSD**"                   */
    unsigned        flags;          /* UFSD_ACTIVE / UFSD_QUIESCE   */
    ECB             wait_ecb;       /* reserved                     */
    UFSD_ANCHOR    *anchor;         /* AP-1b: CSA anchor, or NULL   */
    /* AP-1d: open disk array (STC-local, not CSA) */
    UFSD_DISK      *disks[UFSD_MAX_DISKS];
    unsigned        ndisks;
};

/* ============================================================
** AP-1b: CSA Anchor Block  (~512 bytes, GETMAIN SP=241)
**
** Located via SSCT->ssctsuse. Eye catcher "UFSDANCR".
** ============================================================ */

/* Anchor flags (same bit positions as STC flags for consistency) */
#define UFSD_ANCHOR_ACTIVE   0x80000000U
#define UFSD_ANCHOR_QUIESCE  0x40000000U
#define UFSD_ANCHOR_TRACE_ON 0x20000000U  /* AP-1f: trace ring enabled */

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

    /* AP-1b: SSCT/SSVT (for deregistration on shutdown) */
    SSCT           *ssct;
    SSVT           *ssvt;

    /* AP-1b: base pointers for pool freemain.
    ** Individual items must NOT be freed; only these base pointers
    ** free the entire contiguous pool allocation. */
    UFSREQ         *req_pool_base;
    UFSBUF         *buf_pool_base;

    /* AP-1c: SSI router module loaded into CSA */
    void           *ssir_lpa;       /* UFSDSSIR load module base (freemain) */
    unsigned        ssir_size;      /* UFSDSSIR module size in bytes         */

    /* AP-1c: CS mutex serialising concurrent enqueue producers */
    unsigned        req_lock;       /* 0=unlocked, 1=locked (CS spin)        */

    /* AP-1c: STC ASCB pointer (for __xmpost from client address space) */
    void           *server_ascb;    /* STC ASCB, set at startup              */

    /* AP-1e: STC pointer (for file dispatch access to stc->disks[]) */
    void           *server_stc;     /* UFSD_STC *, set at startup            */
};

/* ============================================================
** AP-1b: Request Block  (~304 bytes, pre-allocated in CSA pool)
**
** Allocated by SSI router in client address space from the
** free pool in the CSA anchor. Chain pointer (next) used for
** both the free pool and the active request queue.
**
** client_ecb_ptr points to a LOCAL ECB variable on the ufsdssir
** stack (key-8 storage in the client address space).  WAIT SVC 1
** on a key-0 CSA ECB from problem state causes X'201'; using a
** key-8 local ECB avoids this.  The STC posts it via __xmpost
** (CVT0PT01) which handles cross-AS key-8 writes.
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
#define UFSREQ_FSEEK        0x0024U  /* AP-1e: seek (deferred)       */
#define UFSREQ_MKDIR        0x0030U  /* AP-1e: make directory        */
#define UFSREQ_CHGDIR       0x0031U  /* AP-1e: change directory      */
#define UFSREQ_RMDIR        0x0032U  /* AP-1e: remove directory      */
#define UFSREQ_REMOVE       0x0033U  /* AP-1e: remove file           */
#define UFSREQ_DIROPEN      0x0040U  /* AP-1f: open directory            */
#define UFSREQ_DIRREAD      0x0041U  /* AP-1f: read directory entry      */
#define UFSREQ_DIRCLOSE     0x0042U  /* AP-1f: close directory handle    */
#define UFSREQ_GETCWD       0x0050U  /* AP-1f: get current work dir  */
#define UFSREQ_MAX          0x00FFU

/* Return codes */
#define UFSD_RC_OK          0    /* success                          */
#define UFSD_RC_NOREQ       4    /* no free request block available  */
#define UFSD_RC_CORRUPT     8    /* corrupt request block (eye fail) */
#define UFSD_RC_BADFUNC     12   /* invalid function code            */
#define UFSD_RC_BADSESS     16   /* invalid session token            */
#define UFSD_RC_INVALID     20   /* validation failed                */
#define UFSD_RC_NOTIMPL     24   /* not yet implemented              */
#define UFSD_RC_NOFILE      28   /* file or path not found           */
#define UFSD_RC_EXIST       32   /* file/dir already exists          */
#define UFSD_RC_NOTDIR      36   /* not a directory                  */
#define UFSD_RC_ISDIR       40   /* is a directory (not a file)      */
#define UFSD_RC_NOSPACE     44   /* no free disk blocks              */
#define UFSD_RC_NOINODES    48   /* no free inodes                   */
#define UFSD_RC_IO          52   /* I/O error                        */
#define UFSD_RC_BADFD       56   /* bad file descriptor              */
#define UFSD_RC_NOTEMPTY    60   /* directory not empty              */
#define UFSD_RC_NAMETOOLONG 64   /* filename exceeds UFSD_NAME_MAX   */

#define UFSREQ_MAX_INLINE   256  /* max bytes in data[] field        */

struct ufsreq {
    char            eye[8];         /* "UFSREQ__"                  */
    UFSREQ         *next;           /* free pool or queue chain    */
    ECB            *client_ecb_ptr; /* -> local ECB in client AS   */
    void           *client_ascb;    /* client ASCB (for cleanup)   */
    unsigned        func;           /* function code (UFSREQ_*)   */
    unsigned        session_token;  /* client session ID           */
    unsigned        client_asid;    /* caller ASID (for cleanup)   */
    int             rc;             /* return code                 */
    int             errno_val;      /* errno                       */
    unsigned        data_len;       /* bytes used in data[]        */
    UFSBUF         *buf;            /* 4K pool buffer (or NULL)    */
    char            data[UFSREQ_MAX_INLINE]; /* inline parm/result */
}; /* ~308 bytes */

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

/* AP-1b: CSA pool sizes */
#define UFSD_REQ_POOL_COUNT  32  /* pre-allocated request blocks   */
#define UFSD_BUF_POOL_COUNT  16  /* pre-allocated 4K buffers       */
#define UFSD_TRACE_SIZE     256  /* trace ring buffer entries      */

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

/* DIRREAD response layout in req->data[] / resp_data[]:
**   [0..3]  = ino (unsigned, 0 = end of directory)
**   [4..7]  = filesize (unsigned)
**   [8..9]  = mode (unsigned short)
**   [10..11]= nlink (unsigned short)
**   [12..71]= name (60 bytes, NUL-terminated)
**   [72..75]= mtime_sec (unsigned)
** Total: 76 bytes */
#define UFSD_DIRREAD_RLEN   76U

/* Global file flags */
#define UFSD_GF_USED        0x80000000U
#define UFSD_GF_DIR         0x20000000U  /* AP-1f: directory handle */

struct ufsd_gfile {
    char            eye[8];         /* "UFSDGFIL"                  */
    unsigned        flags;          /* UFSD_GF_*                   */
    int             disk_idx;       /* index into stc->disks[]     */
    unsigned        ino;            /* UFS inode number            */
    unsigned        position;       /* current file offset (bytes) */
    unsigned        open_mode;      /* UFSD_OPEN_* flags           */
    unsigned        refcount;       /* always 1 in Phase 1         */
};

/* ============================================================
** AP-1c: SSOB extension for UFSD requests
**
** Pointed to by SSOB.SSOBINDV.  The client fills func, token,
** data_len, and data[] before calling iefssreq().  The router
** fills rc, errno_val, and data[] (result) before returning.
** ============================================================ */

#define UFSSSOB_EYE     "UFSS"
#define UFSD_SSOBFUNC   128U    /* SSOB function code for all UFSD ops */
#define UFSD_SSVT_ROUTER  1U   /* SSVT index of the thin router        */

typedef struct ufsssob  UFSSSOB;
struct ufsssob {
    char            eye[4];         /* "UFSS"                           */
    unsigned        func;           /* UFSREQ_* function code           */
    unsigned        token;          /* session token (0 = no session)   */
    int             rc;             /* return code (output)    off=+12  */
    int             errno_val;      /* errno (output)                   */
    unsigned        data_len;       /* bytes used in data[]             */
    char            data[UFSREQ_MAX_INLINE]; /* parameters / result    */
    ECB            *client_ecb;     /* ptr to client-private ECB (input)
                                    ** MUST stay after data[]: iefssreq
                                    ** reads SSOBINDV+12 as R1 for the
                                    ** SSVT call; putting client_ecb at
                                    ** offset 12 passes &ECB instead of
                                    ** the SSOB address -> S0C4.        */
    void           *buf_ptr;        /* client buffer for 4K transfers:
                                    ** FREAD: destination in caller AS
                                    ** FWRITE: source in caller AS
                                    ** NULL = use inline data[] path    */
    unsigned        buf_len;        /* capacity of buf_ptr in bytes     */
};

/* ============================================================
** Function Prototypes
** ============================================================ */

/* ufsd#cmd.c (AP-1a) */
int  ufsd_process_cib(UFSD_STC *ufsd, CIB *cib);

/* ufsd.c (AP-1a) */
void ufsd_shutdown(UFSD_STC *ufsd);

/* ufsd#csa.c (AP-1b) */
UFSD_ANCHOR *ufsd_anchor_alloc(void)                                 asm("UFSD@ANA");
void         ufsd_anchor_free(UFSD_ANCHOR *anchor)                   asm("UFSD@ANF");
int          ufsd_csa_init(UFSD_ANCHOR *anchor)                      asm("UFSD@CAI");
void         ufsd_csa_free(UFSD_ANCHOR *anchor)                      asm("UFSD@CAF");
UFSREQ      *ufsd_req_alloc(UFSD_ANCHOR *anchor)                     asm("UFSD@RQA");
void         ufsd_req_free(UFSD_ANCHOR *anchor, UFSREQ *req)         asm("UFSD@RQF");

/* ufsd#buf.c — buffer pool alloc/free (split from ufsd#csa.c) */
UFSBUF      *ufsd_buf_alloc(UFSD_ANCHOR *anchor)                     asm("UFSD@BFA");
void         ufsd_buf_free(UFSD_ANCHOR *anchor, UFSBUF *buf)         asm("UFSD@BFF");

/* ufsd#sct.c (AP-1b) */
int  ufsd_ssct_init(UFSD_ANCHOR *anchor)                             asm("UFSD@SSI");
void ufsd_ssct_free(UFSD_ANCHOR *anchor)                             asm("UFSD@SSF");

/* ufsd#sct.c (AP-1c) */
int  ufsd_ssi_load(UFSD_ANCHOR *anchor)                              asm("UFSD@SLA");
void ufsd_ssi_unload(UFSD_ANCHOR *anchor)                            asm("UFSD@SLF");

/* ufsd#trc.c (AP-1b) */
void ufsd_trace(UFSD_ANCHOR *anchor, unsigned short func,
                unsigned token, unsigned short rc)                   asm("UFSD@TRC");

/* ufsd#trc.c (AP-1f) */
void ufsd_trace_dump(UFSD_ANCHOR *anchor)                            asm("UFSD@TRD");

/* ufsd#que.c (AP-1c) */
UFSREQ *ufsd_dequeue(UFSD_ANCHOR *anchor)                            asm("UFSD@DEQ");
void    ufsd_dispatch(UFSD_ANCHOR *anchor, UFSREQ *req)              asm("UFSD@DSP");
void    ufsd_server_ecb_reset(UFSD_ANCHOR *anchor)                   asm("UFSD@ECR");

/* ufsd#ses.c (AP-1d) */
int           ufsd_sess_init(UFSD_ANCHOR *anchor)                    asm("UFSD@SIN");
void          ufsd_sess_free(UFSD_ANCHOR *anchor)                    asm("UFSD@SFR");
int           ufsd_sess_open(UFSD_ANCHOR *anchor, UFSREQ *req,
                             unsigned *out_token)                     asm("UFSD@SOP");
int           ufsd_sess_close(UFSD_ANCHOR *anchor, UFSREQ *req)      asm("UFSD@SCL");
void          ufsd_sess_list(UFSD_ANCHOR *anchor)                    asm("UFSD@SLS");
UFSD_SESSION *ufsd_sess_find(UFSD_ANCHOR *anchor, unsigned token)    asm("UFSD@SFN");

/* ufsd#ini.c (AP-1d Step 2) */
int           ufsd_ufs_init(UFSD_STC *stc)                          asm("UFSD@UNI");
void          ufsd_ufs_term(UFSD_STC *stc)                          asm("UFSD@UNT");

/* ufsd#ini.c (AP-1f) -- dynamic mount/unmount */
int           ufsd_disk_mount(UFSD_STC *stc, const char *ddname,
                              const char *mountpath)                 asm("UFSD@DMT");
int           ufsd_disk_umount(UFSD_STC *stc,
                               const char *mountpath)                asm("UFSD@DUT");

/* ufsd#blk.c (AP-1e) -- BDAM block I/O */
int  ufsd_blk_read(UFSD_DISK *disk, unsigned sector, void *buf)      asm("UFSD@BRD");
int  ufsd_blk_write(UFSD_DISK *disk, unsigned sector, void *buf)     asm("UFSD@BWR");

/* ufsd#sbl.c (AP-1e) -- superblock management */
int  ufsd_sb_read(UFSD_DISK *disk)                                   asm("UFSD@SBR");
int  ufsd_sb_write(UFSD_DISK *disk)                                  asm("UFSD@SBW");
int  ufsd_sb_alloc_block(UFSD_DISK *disk, unsigned *out_sector)      asm("UFSD@SAB");
void ufsd_sb_free_block(UFSD_DISK *disk, unsigned sector)            asm("UFSD@SFB");
int  ufsd_sb_alloc_inode(UFSD_DISK *disk, unsigned *out_ino)         asm("UFSD@SAI");
void ufsd_sb_free_inode(UFSD_DISK *disk, unsigned ino)               asm("UFSD@SFI");

/* ufsd#ino.c (AP-1e) -- inode I/O */
int  ufsd_ino_read(UFSD_DISK *disk, unsigned ino, UFSD_DINODE *out)           asm("UFSD@INR");
int  ufsd_ino_write(UFSD_DISK *disk, unsigned ino, const UFSD_DINODE *in)     asm("UFSD@INW");

/* ufsd#dir.c (AP-1e) -- directory and path operations */
unsigned ufsd_dir_lookup(UFSD_DISK *disk, unsigned dir_ino,
                         const char *name)                            asm("UFSD@DLU");
int      ufsd_dir_add(UFSD_DISK *disk, unsigned dir_ino,
                      const char *name, unsigned ino)                 asm("UFSD@DAD");
int      ufsd_dir_remove(UFSD_DISK *disk, unsigned dir_ino,
                         const char *name)                            asm("UFSD@DRM");
unsigned ufsd_path_lookup(UFSD_DISK *disk, unsigned start_ino,
                          const char *path,
                          unsigned *out_parent_ino,
                          char *out_name)                             asm("UFSD@PLU");

/* ufsd#gft.c (AP-1e) -- global file table */
int  ufsd_gft_init(UFSD_ANCHOR *anchor)                              asm("UFSD@GTI");
void ufsd_gft_free(UFSD_ANCHOR *anchor)                              asm("UFSD@GTF");
int  ufsd_gft_alloc(UFSD_ANCHOR *anchor, unsigned *out_idx)          asm("UFSD@GTA");
void ufsd_gft_release(UFSD_ANCHOR *anchor, unsigned idx)             asm("UFSD@GTR");

/* ufsd#fil.c (AP-1e) -- file operation dispatch */
int  ufsd_fil_dispatch(UFSD_ANCHOR *anchor, UFSD_SESSION *sess,
                       UFSREQ *req,
                       char *resp_data, unsigned *resp_data_len)      asm("UFSD@FDS");

#endif /* UFSD_H */
