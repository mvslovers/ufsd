# UFS370 STC/SUBSYS Design — Concept #4

Cross-Address-Space Filesystem Server for MVS 3.8j  
Repository: github.com/mvslovers/ufs370  
Revision 4 — Operability, Diagnostics & Future Direction

---

## 1. Problem Statement

UFS370 is a Unix-like virtual filesystem for MVS 3.8j backed by BDAM datasets. All filesystem structures (UFSSYS, disk handles, pager caches, inodes, mountpoints) are bound to a single address space.

The root cause is in `ufssyget.c`:

```c
static UFSSYS ufssykey = {0};
UFSSYS *ufs_sys_get(void) {
    return (UFSSYS *) __wsaget(&ufssykey, sizeof(UFSSYS));
}
```

`__wsaget()` ties the UFSSYS instance to the current address space. BDAM DCBs are also address-space-local. No external address space can access the filesystem.

## 2. Existing Architecture — Strengths

Clean separation between "System" (UFSSYS) and "User Session" (UFS handle):

### UFSSYS — Global Filesystem State

| Field      | Type         | Purpose                      |
|------------|--------------|------------------------------|
| disks      | UFSDISK **   | BDAM disk handles (DCBs)     |
| pagers     | UFSPAGER **  | Block-level caches           |
| vdisks     | UFSVDISK **  | Logical disk handles         |
| fsroot     | UFSMIN *     | Root inode "/"               |
| mountpoint | UFSMIN **    | Mounted directory inodes     |

### UFS — Per-User Session Handle (32 bytes)

```c
struct ufs {
    char     eye[8];         /* "**UFS**" */
    UFSSYS   *sys;           /* -> shared filesystem  */
    UFSCWD   *cwd;           /* per-user working dir  */
    ACEE     *acee;          /* per-user RACF context */
    UINT32   create_perm;    /* per-user umask        */
};
```

This separation already forms an implicit kernel/process model — ideal for a session-token-based client/server design.

### Existing Mount System

`ufs_sys_new()` already supports multiple BDAM disks (UFSDISK0–9) with `/etc/fstab`-based mounting. The first disk becomes root, additional disks are mounted via fstab entries. Mount tracking uses `UFSMIN->mounted_vdisk`. This mechanism carries over into the STC model unchanged — the DD cards simply move to the STC JCL proc.

## 3. Available Mechanisms on MVS 3.8j

**Constraint:** No Cross-Memory Services (PC/PT, ALESERV) on S/370. Available:

- **SSCT/SSCVT** — Subsystem registration (JESCT chain)
- **IEFSSREQ SVC 34** — Subsystem Request Interface
- **CSA/SQA** — GETMAIN SP=241/245, visible cross-address-space
- **POST/WAIT** — ECB-based synchronization
- **CS Instruction** — Compare-and-Swap for lock-free operations (native S/370)
- **ENQ/DEQ** — Shared resource serialization
- **QEDIT/CIB** — Operator command interface for STCs

## 4. Architecture: UFSD Subsystem

### 4.1 Overview

```
┌───────────────────────┐     ┌──────────────────────────┐
│ Client Address Space  │     │  UFSD STC Address Space  │
│                       │     │                          │
│  Application          │     │  UFSSYS (fully local)    │
│    │                  │     │    ├─ Disks (BDAM DCBs)  │
│    ▼                  │     │    ├─ Pager Caches       │
│  Client-Stub (libufs) │     │    └─ Inodes/Mounts     │
│    │                  │     │                          │
│    ▼                  │     │  Session Table           │
│  IEFSSREQ (SVC 34)   │     │    ├─ fd_table[64]       │
│    │                  │     │    └─ ACEE/CWD           │
│    ▼                  │     │  Global Open File Table  │
│  SSI Thin Router      │     │  Dispatch + Workers      │
│    │ (enqueue/POST/   │     │                          │
│    │  WAIT only)      │     │  MODIFY Command Handler  │
│    ▼                  │     │  (CIB/QEDIT)            │
│  ┌─────── CSA ────────────────────────────────┐ │
│  │  UFSD_ANCHOR                               │ │
│  │  HEAD/TAIL (CS lock-free)                  │ │
│  │  server_ecb                                │ │
│  │  Request Pool + 4K Bufs                    │ │
│  │  Trace Ring Buffer                         │ │
│  └────────────────────────────────────────────┘ │
└───────────────────────┘     └──────────────────────────┘
```

### 4.2 SSCT Registration

#### 4.2.1 Programmatic Registration (Phase 1)

```c
void ufsd_register_subsystem(void) {
    CVT   *cvt   = *(CVT**)0x10;
    JESCT *jesct = cvt->cvtjesct;

    SSCT *ssct = __getmain(241, sizeof(SSCT));
    memcpy(ssct->ssctid,  "SSCT", 4);
    memcpy(ssct->ssctnam, "UFSD", 4);

    SSCVT *sscvt = __getmain(241, sizeof(SSCVT));
    sscvt->sscvtfr = &ufsd_ssi_router;
    ssct->ssctssvt = sscvt;

    ssct->ssctscta = jesct->jesssct;
    jesct->jesssct = ssct;
}
```

#### 4.2.2 IEFSSNxx Registration (optional, later phase)

Static registration via `SYS1.PARMLIB(IEFSSNxx)`:

```
SUBSYS SUBNAME(UFSD)
       INITRTN(UFSDINIT)
       INITPARM('AUTOSTART')
```

**Constraint:** IEFSSNxx semantics on MVS 3.8j Turnkey systems (Hercules) may differ from later MVS/SP. The INITRTN call timing and available services during NIP must be verified before adoption. Programmatic registration (4.2.1) remains the safe path for Phase 1.

### 4.3 SSI Thin Router

Runs in the **client address space**. Contains **zero business logic** — exactly three tasks:

1. Enqueue request block into CSA queue (CS lock-free)
2. Conditional POST: only if queue was previously empty
3. WAIT on client ECB

```c
/* SSI Thin Router — ZERO business logic */
int ufsd_ssi_router(SSOB *ssob) {
    UFSD_ANCHOR *anchor = get_ufsd_anchor();
    UFSSSOB *ussob = (UFSSSOB *)ssob->ssobindv;

    UFSREQ *req = ufsd_alloc_request(anchor);
    if (!req) {
        ussob->rc = UFSD_RC_NOREQ;
        return 4;
    }

    req->func          = ussob->function;
    req->session_token = ussob->session;
    req->data_len      = ussob->parmlen;
    req->client_ecb    = 0;
    req->client_asid   = current_asid();
    memcpy(req->data, ussob->parmdata,
           MIN(ussob->parmlen, UFSREQ_MAX_INLINE));

    /* Lock-free enqueue, returns old_tail */
    int was_empty = ufsd_enqueue(anchor, req);

    /* POST only if queue was empty — saves SVC overhead under load */
    if (was_empty)
        POST(&anchor->server_ecb, 0);

    WAIT(1, &req->client_ecb);

    ussob->rc        = req->rc;
    ussob->errno_val = req->errno_val;
    ussob->data_len  = req->data_len;
    memcpy(ussob->parmdata, req->data, req->data_len);

    ufsd_free_request(anchor, req);
    return 0;
}
```

**Why no TS spinlock:** If a client is interrupted while holding a TS lock (timer interrupt, page fault), all other clients spin in busy-wait. CS-based lock-free enqueue eliminates this entirely — no lock, no spin, no deadlock.

#### 4.3.1 SSI Residency Strategy

| Variant       | Mechanism                                    | Use Case                              |
|---------------|----------------------------------------------|---------------------------------------|
| **Development** | GETMAIN SP=241 + LOAD into CSA. No IPL needed. | Iterative dev on Hercules.           |
| **Production**  | IEALPAxx, module in SYS1.LPALIB. Requires IPL.  | Stable ops, no CSA for code.         |

### 4.4 CSA Request Queue

#### 4.4.1 Anchor Block

```c
typedef struct ufsd_anchor {
    char     eye[8];        /* "UFSDANCR"              */
    UINT32   version;       /* anchor version          */
    UINT32   flags;         /* status flags            */
#define UFSD_ACTIVE   0x80000000
#define UFSD_QUIESCE  0x40000000
    ECB      server_ecb;    /* master ECB for STC      */

    /* Request Queue (CS lock-free) */
    UFSREQ   *req_head;
    UFSREQ   *req_tail;

    /* Free Pool */
    UFSREQ   *free_head;
    UINT32   free_count;
    UINT32   total_reqs;

    /* Buffer Pool */
    UFSBUF   *buf_free;
    UINT32   buf_count;
    UINT32   buf_total;

    /* Session + Global File Tables */
    UFSD_SESSION *sessions;
    UINT32   max_sessions;
    UFSD_GFILE  *gfiles;
    UINT32   max_gfiles;

    /* Trace Ring Buffer */
    UFSD_TRACE  *trace_buf;  /* -> trace ring       */
    UINT32   trace_next;     /* next write index    */
    UINT32   trace_size;     /* ring size (entries) */

    /* Statistics */
    UINT32   stat_requests;
    UINT32   stat_errors;
    UINT32   stat_posts_saved;  /* conditional POST  */
} UFSD_ANCHOR;
```

#### 4.4.2 Request Block

```c
typedef struct ufsreq {
    char     eye[8];        /* "UFSREQ__"              */
    UFSREQ   *next;         /* chain pointer           */
    ECB      client_ecb;    /* POST'd on completion    */
    UINT32   func;          /* function code           */
    UINT32   session_token; /* client session ID       */
    UINT32   client_asid;   /* caller ASID             */
    INT32    rc;            /* return code             */
    INT32    errno_val;     /* errno                   */
    UINT32   data_len;      /* inline data length      */
    UFSBUF   *buf;          /* -> 4K buffer (or NULL)  */
    char     data[256];     /* inline parm area        */
} UFSREQ;  /* ~304 bytes */
```

#### 4.4.3 Inline-Buffer Optimization

| Operation             | Typical Size | Buffer Strategy      |
|-----------------------|--------------|----------------------|
| fopen (path + mode)   | ~140 bytes   | **Inline**           |
| mkdir/chgdir/rmdir    | ~130 bytes   | **Inline**           |
| dirread (1 entry)     | ~80 bytes    | **Inline**           |
| fread/fwrite (<256 B) | <256 bytes   | **Inline**           |
| fread/fwrite (>256 B) | up to 4096   | **4K Pool Buffer**   |
| fread/fwrite (>4K)    | >4096 bytes  | **Chunked**, 4K each |

>80% of all requests fit inline. The 4K pool is primarily needed for bulk data transfer.

#### 4.4.4 Lock-Free Enqueue with POST Bundling

```c
/* Lock-free enqueue, returns 1 if queue was empty */
int ufsd_enqueue(UFSD_ANCHOR *anchor, UFSREQ *req) {
    UFSREQ *old_tail;
    req->next = NULL;
    do {
        old_tail = anchor->req_tail;
    } while (__cs(&old_tail,
                  &anchor->req_tail, req) != 0);
    if (old_tail) {
        old_tail->next = req;
        return 0;  /* queue was not empty */
    } else {
        anchor->req_head = req;
        return 1;  /* queue was empty, POST needed */
    }
}
```

Under load with e.g. 5 simultaneous clients, only 1 POST SVC fires instead of 5. The `stat_posts_saved` counter in the anchor tracks savings for performance analysis.

### 4.5 Request Validation

Every request block is validated before dispatch. A corrupt CSA pointer must not damage the filesystem:

```c
int ufsd_validate_request(UFSREQ *req) {
    if (memcmp(req->eye, "UFSREQ__", 8) != 0) {
        ufsd_trace(UFSD_T_CORRUPT, 0, 0);
        return UFSD_RC_CORRUPT;
    }
    if (req->func < UFSREQ_MIN || req->func > UFSREQ_MAX) {
        ufsd_trace(UFSD_T_BADFUNC, req->func, 0);
        return UFSD_RC_BADFUNC;
    }
    if (!ufsd_session_valid(req->session_token)) {
        ufsd_trace(UFSD_T_BADSESS, req->session_token, 0);
        return UFSD_RC_BADSESS;
    }
    return 0;
}
```

### 4.6 Diagnostic Trace Ring Buffer

Essential for post-mortem analysis on MVS 3.8j without a modern debugger.

```c
typedef struct ufsd_trace_entry {
    UINT32   timestamp;     /* STCK low-word or TOD  */
    UINT16   func;          /* function code         */
    UINT16   rc;            /* return code           */
    UINT32   session_token; /* who                   */
    UINT32   asid;          /* which address space   */
} UFSD_TRACE;  /* 16 bytes per entry */
```

Standard: 256 entries = 4K. Debug: 1024 entries = 16K.

```c
void ufsd_trace(UINT16 func, UINT32 token, UINT16 rc) {
    UFSD_ANCHOR *a = get_ufsd_anchor();
    UINT32 idx = a->trace_next;
    a->trace_next = (idx + 1) % a->trace_size;

    UFSD_TRACE *t = &a->trace_buf[idx];
    t->timestamp     = __stck_low();
    t->func          = func;
    t->rc            = rc;
    t->session_token = token;
    t->asid          = current_asid();
}
```

Trace categories:

| Constant         | Value  | Meaning                  |
|------------------|--------|--------------------------|
| UFSD_T_REQUEST   | 0x0001 | Request received         |
| UFSD_T_COMPLETE  | 0x0002 | Request completed        |
| UFSD_T_SESS_OPEN | 0x0010 | Session opened           |
| UFSD_T_SESS_CLOSE| 0x0011 | Session closed           |
| UFSD_T_SESS_ABEND| 0x0012 | Session cleanup (ABEND)  |
| UFSD_T_CORRUPT   | 0x00F0 | Corrupt request block    |
| UFSD_T_BADFUNC   | 0x00F1 | Invalid function code    |
| UFSD_T_BADSESS   | 0x00F2 | Invalid session token    |
| UFSD_T_MODIFY    | 0x0020 | Operator MODIFY command  |

### 4.7 CSA Memory Budget

| Resource              | Count | Size/each  | CSA Total |
|-----------------------|-------|------------|-----------|
| UFSD Anchor           | 1     | ~512 B     | ~1K       |
| Request-Block Pool    | 32    | ~304 B     | ~10K      |
| 4K Data Buffer Pool   | 16    | 4096 B     | ~64K      |
| Session Table         | 64    | ~160 B     | ~10K      |
| Global Open File Table| 256   | ~32 B      | ~8K       |
| Trace Ring Buffer     | 256   | 16 B       | ~4K       |
| SSI Routine (dev only)| 1     | ~4K        | ~4K       |
| **Total (steady-state)** |    |            | **~101K** |

## 5. Multi-Client File Descriptor Model

Two-level model analogous to the UNIX kernel:

```
Client fd → Session fd_table[64] → Global Open File → Inode
```

### 5.1 Data Structures

#### 5.1.1 Session (STC-local)

```c
typedef struct ufsd_session {
    char     eye[8];         /* "UFSDSESS"            */
    UINT32   token;          /* session token          */
    UINT32   client_asid;    /* client address space   */
    UINT32   flags;
#define UFSD_SESS_ACTIVE  0x80000000
#define UFSD_SESS_CLEANUP 0x40000000
    UFS      *ufs;           /* server-side UFS handle */
    UFSD_FD  fd_table[UFSD_MAX_FD];
} UFSD_SESSION;

#define UFSD_MAX_FD  64
```

#### 5.1.2 Per-Session File Descriptor

```c
typedef struct ufsd_fd {
    UINT32   gfile_idx;      /* index in global table  */
#define UFSD_FD_UNUSED  0xFFFFFFFF
    UINT32   flags;
#define UFSD_FD_READ    0x80000000
#define UFSD_FD_WRITE   0x40000000
#define UFSD_FD_APPEND  0x20000000
} UFSD_FD;
```

#### 5.1.3 Global Open File Entry (STC-local)

```c
typedef struct ufsd_gfile {
    char     eye[8];         /* "UFSDGFIL"            */
    UINT32   flags;
#define UFSD_GF_USED    0x80000000
    UFSMIN   *minode;        /* -> memory inode        */
    UFSVDISK *vdisk;         /* -> virtual disk        */
    UINT32   position;       /* file offset            */
    UINT32   open_mode;
    UINT32   refcount;       /* sessions referencing   */
} UFSD_GFILE;
```

**No shared position:** MVS has no fork(). Every ufs_fopen() creates an independent Global File Entry with its own position pointer. Refcount is always 1 in Phase 1.

### 5.2 Session Lifecycle

#### Create (UFSREQ_SESS_OPEN)
1. Find free session slot
2. Generate token (counter + validation byte)
3. `ufsnew()` → server-side UFS handle
4. Set ACEE
5. Initialize fd_table with UFSD_FD_UNUSED
6. Return token to client

#### Close (UFSREQ_SESS_CLOSE)
1. Close all open fds
2. `ufsfree()` server-side handle
3. Release session slot

#### Cleanup on Client ABEND
```
for each active session:
    if ASCB for session->client_asid terminated:
        close all fds, release inodes
        ufsfree(session->ufs)
        trace(UFSD_T_SESS_ABEND, ...)
        release session slot
```

## 6. Operator Command Interface

Commands via MODIFY (`/F UFSD,<command>`) using CIB/QEDIT:

| Command                       | Description                                              |
|-------------------------------|----------------------------------------------------------|
| `/F UFSD,MOUNT DD=x,PATH=y`  | Mount BDAM dataset on path                               |
| `/F UFSD,UNMOUNT PATH=y`     | Unmount path (fsync first)                               |
| `/F UFSD,STATS`              | Statistics: sessions, files, cache, requests, saved POSTs|
| `/F UFSD,SESSIONS`           | List active sessions with ASID, FDs, token               |
| `/F UFSD,TRACE ON\|OFF\|DUMP`| Control trace ring buffer                                |
| `/F UFSD,SHUTDOWN`           | Orderly shutdown                                         |
| `/P UFSD`                    | Standard MVS STOP (equivalent to SHUTDOWN)               |

### CIB/QEDIT Processing

```c
void ufsd_check_commands(UFSD_ANCHOR *anchor) {
    IEZCOM  *com;
    CIB     *cib;

    com = get_com_area();
    cib = com->comcibpt;

    while (cib) {
        if (cib->cibverb == CIB_MODIFY) {
            char *parm = &cib->cibdata;
            int  plen  = cib->cibdatln;
            ufsd_process_command(anchor, parm, plen);
        }
        else if (cib->cibverb == CIB_STOP) {
            anchor->flags |= UFSD_QUIESCE;
            anchor->flags &= ~UFSD_ACTIVE;
        }

        QEDIT(cib, CIBCTR=FREE);
        cib = com->comcibpt;
    }
}
```

### Main Loop Integration

```c
void ufsd_main_loop(UFSD_ANCHOR *anchor) {
    QEDIT(ORIGIN=comaddr, CIBCTR=1);

    while (anchor->flags & UFSD_ACTIVE) {
        WAIT(1, &anchor->server_ecb);
        anchor->server_ecb = 0;

        ufsd_check_commands(anchor);

        UFSREQ *req;
        while ((req = ufsd_dequeue(anchor))) {
            if (ufsd_validate_request(req) == 0)
                ufsd_dispatch(req);
            else
                req->rc = UFSD_RC_INVALID;
            POST(&req->client_ecb, 0);
        }
    }

    ufsd_shutdown(anchor);
}
```

## 7. Dispatch Model

### 7.1 Phase 1: Single-Threaded (PoC)

As shown in section 6 main loop. Request processing and command handling in the same thread.

**Limitation:** Slow BDAM I/O blocks all waiting clients.

### 7.2 Target: Multi-Worker via ATTACH (Phase 3)

```
Dispatch Loop (Main Task)
    │
    ├─ WAIT on server_ecb
    ├─ ufsd_check_commands()
    ├─ Dequeue Request
    ├─ Validate Request
    ├─ Assign to free worker, POST worker_ecb
    └─ Loop

Worker (pre-ATTACHed subtask, pool of 4–8)
    │
    ├─ WAIT on worker_ecb
    ├─ ufsd_dispatch(req)
    ├─ POST req->client_ecb
    └─ Return to pool
```

**Prerequisite:** Inode R/W locking, Global File Table concurrency.

## 8. Concurrency (Phase 3)

Existing in UFS370 code:
- `ufs_pager_lock()` / `ufs_page_lock()` — pager level
- `UFSMIN->usecount` — inode reference counting

Additionally needed:
- **Inode R/W-Lock:** ENQ QNAME='UFSD', RNAME=VDisk#+Inode#
- **Directory Lock:** Exclusive for mkdir/rmdir/rename
- **Global File Table:** CS on gfile slot allocation

## 9. ABEND Recovery

| Strategy   | Mechanism                                                        | Assessment                           |
|------------|------------------------------------------------------------------|--------------------------------------|
| **ASID-Scan** | Periodically check if session ASIDs still have active ASCBs.  | **Recommended.** Primary mechanism.  |
| **ETXR**      | Client stub registers end-of-task recovery. Immediate notification. | Addition in Phase 3.              |

## 10. Future Direction: VFS Abstraction (Phase 4+)

UFS370 already has a rudimentary filesystem abstraction: `UFSIO` function pointers in UFSVDISK and name-to-VDisk mappings in UFSSYS allow multiple mounted disk datasets. For long-term extensibility, a full VFS layer could be introduced.

```c
struct ufs_vfs_ops {
    int (*vfs_open)(VFS *vfs, const char *path, ...);
    int (*vfs_read)(VFS *vfs, GFILE *gf, void *buf, ...);
    int (*vfs_write)(VFS *vfs, GFILE *gf, void *buf, ...);
    int (*vfs_stat)(VFS *vfs, const char *path, ...);
    int (*vfs_mkdir)(VFS *vfs, const char *path, ...);
};
```

Potential future filesystem types: RAMFS (memory-only), DEVFS (pseudo-devices).

**Status:** Not part of current design. Prerequisite is a stable STC/SUBSYS layer (Phases 1–3). The existing UFSIO abstraction provides a good anchor point.

## 11. Phase Plan

| Phase | Scope                    | Deliverables                                                       |
|-------|--------------------------|--------------------------------------------------------------------|
| **1** | STC + CSA PoC            | STC skeleton, SSCT, CSA anchor + queue (CS + POST bundling), SSI thin router, single-thread dispatch, sessions + fd_table + global file table, trace buffer, request validation, MODIFY commands, ASID-scan cleanup |
| **2** | SSI + Stubs              | ~30 client stub functions, inline-buffer + 4K pool, marshalling, optional IEFSSNxx |
| **3** | Multi-Worker + Concurrency | Pre-ATTACHed worker pool, inode/directory R/W locking, ETXR, SSI to LPA, stress tests |
| **4+**| Future / VFS             | VFS abstraction, RAMFS/DEVFS, dup() support, extended RACF, perf tuning |
