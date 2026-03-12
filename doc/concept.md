# UFS370 STC/SUBSYS Design — Concept #5

Cross-Address-Space Filesystem Server for MVS 3.8j  
Repository: github.com/mvslovers/ufsd  
Revision 5 — Updated with Implementation Learnings (AP-1a through AP-1e)

---

## Changes from Concept #4

| # | Area | Change |
|---|------|--------|
| 1 | Cross-AS POST | Replaced `ecb_post` (SVC 2) with `__xmpost` (CVT0PT01 branch entry). SVC 2 cannot post ECBs cross-address-space from problem state. |
| 2 | Client ECB | Moved from CSA (UFSREQ field) to **local stack variable** in SSI router. WAIT on key-0 ECB from problem state → X'201'. Local ECB is key-8, works correctly. |
| 3 | ASCB tracking | Added `server_ascb` to UFSD_ANCHOR, `client_ascb` to UFSREQ. Required by `__xmpost`. |
| 4 | SSI Router entry | Declared as `void ufsdssir(void)`. R1=SSOB extracted via inline asm before any C plist dereference. MVS SSI convention passes R1=SSOB directly, not as C plist. |
| 5 | Supervisor/problem state | SSI router runs in supervisor state (key-0) for `__xmpost`, then switches to problem state for WAIT. Documented state transitions. |
| 6 | UFS reimplementation | UFSD reimplements block I/O, inode, directory, superblock layers directly instead of linking UFS370 library. Required because UFS370's `__wsaget()` binding and DCB management are address-space-local. |
| 7 | ESTAE requirement | ESTAE exit required in STC to ensure SSCT deregistration on abend. Without it, SSCT remains registered and blocks restart until IPL. |
| 8 | Module naming | Source files use `ufsd#xxx.c` convention (MVS 8-character member names). |

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

This separation forms an implicit kernel/process model.

### Existing Mount System

`ufs_sys_new()` supports multiple BDAM disks (UFSDISK0–9) with `/etc/fstab`-based mounting. The first disk becomes root, additional disks are mounted via fstab entries. Mount tracking uses `UFSMIN->mounted_vdisk`. This mechanism carries over into the STC model — the DD cards move to the STC JCL proc.

### Why UFSD Reimplements Disk I/O

The UFSD STC does **not** link against the UFS370 library. Instead, it reimplements the block I/O, inode, directory, and superblock layers directly. Reason: UFS370's internal state management (`__wsaget()` for UFSSYS, array-based handle tracking, pager integration) is deeply tied to running within a single address space. Extracting individual functions while bypassing the state management would require invasive modifications to UFS370. A clean reimplementation of the on-disk format handling (which is well-defined and stable) is simpler and more maintainable.

The reimplemented modules (`ufsd#blk`, `ufsd#sbl`, `ufsd#ino`, `ufsd#dir`) operate directly on BDAM DCBs owned by the STC and implement only the subset needed for the server: sector read/write, superblock management, inode I/O with direct-block addressing, and directory path resolution.

## 3. Available Mechanisms on MVS 3.8j

**Constraint:** No Cross-Memory Services (PC/PT, ALESERV) on S/370. Available:

- **SSCT/SSCVT** — Subsystem registration (JESCT chain)
- **IEFSSREQ SVC 34** — Subsystem Request Interface
- **CSA/SQA** — GETMAIN SP=241/245, visible cross-address-space
- **__xmpost (CVT0PT01)** — Cross-address-space POST via branch entry (**not** SVC 2)
- **WAIT (SVC 1)** — Must be on key-8 ECB from problem state
- **CS Instruction** — Compare-and-Swap for lock-free operations (native S/370)
- **ENQ/DEQ** — Shared resource serialization
- **QEDIT/CIB** — Operator command interface for STCs
- **ESTAE** — Task-level abend recovery

### Cross-AS Communication Constraints (Learned in AP-1c)

| Operation | Mechanism | Required State | Constraint |
|-----------|-----------|----------------|------------|
| POST cross-AS | `__xmpost(ascb, ecb_ptr, code)` via CVT0PT01 | Supervisor (key 0) | SVC 2 (ecb_post) fails cross-AS from problem state → S102 |
| WAIT on ECB | `WAIT ECB=(ecb_addr)` via SVC 1 | Problem state | ECB must be in key-8 storage. Key-0 ECB → X'201' |
| SSI routine entry | R1 = SSOB address (MVS convention) | Supervisor (key 0) | NOT a C parameter list. Must extract via inline asm. |

## 4. Architecture: UFSD Subsystem

### 4.1 Overview

```
┌───────────────────────┐     ┌──────────────────────────┐
│ Client Address Space  │     │  UFSD STC Address Space  │
│                       │     │                          │
│  Application          │     │  Disk I/O Layer          │
│    │                  │     │    ├─ BDAM DCBs          │
│    ▼                  │     │    ├─ Superblock mgmt    │
│  Client-Stub (libufs) │     │    ├─ Inode I/O          │
│    │                  │     │    └─ Directory ops      │
│    ▼                  │     │                          │
│  IEFSSREQ (SVC 34)   │     │  Session Table           │
│    │                  │     │    ├─ fd_table[64]       │
│    ▼                  │     │    └─ CWD                │
│  SSI Router           │     │  Global Open File Table  │
│  (ufsdssir)           │     │  Dispatch + MODIFY Cmds  │
│    │ R1=SSOB          │     │                          │
│    │ __xmpost to wake │     │  ESTAE recovery          │
│    │ WAIT on local ECB│     │                          │
│    ▼                  │     │                          │
│  ┌─────── CSA ────────────────────────────────┐       │
│  │  UFSD_ANCHOR                               │       │
│  │  ├─ HEAD/TAIL (CS lock-free)               │       │
│  │  ├─ server_ecb (key-0, STC WAITs on this)  │       │
│  │  ├─ server_ascb (for __xmpost)             │       │
│  │  ├─ Request Pool (32 × ~296B)              │       │
│  │  ├─ 4K Buffer Pool (16 × 4K)              │       │
│  │  └─ Trace Ring Buffer (256 × 16B)         │       │
│  └────────────────────────────────────────────┘       │
└───────────────────────┘     └──────────────────────────┘
```

### 4.2 SSCT Registration

#### 4.2.1 Programmatic Registration (Phase 1)

```c
void ufsd_register_subsystem(UFSD_ANCHOR *anchor) {
    CVT   *cvt   = *(CVT**)0x10;
    JESCT *jesct = cvt->cvtjesct;

    SSCT *ssct = __getmain(241, sizeof(SSCT));
    memcpy(ssct->ssctid,  "SSCT", 4);
    memcpy(ssct->ssctnam, "UFSD", 4);

    SSCVT *sscvt = __getmain(241, sizeof(SSCVT));
    sscvt->sscvtfr = &ufsdssir;  /* SSI router entry point */
    ssct->ssctssvt = sscvt;

    ssct->ssctscta = jesct->jesssct;
    jesct->jesssct = ssct;

    /* Store SSCT pointer for deregistration */
    anchor->ssct = ssct;
}
```

**Known behavior:** While UFSD is registered as a subsystem, issuing `S UFSD` a second time fails with IEF612I PROCEDURE NOT FOUND. MVS routes system-internal SSI calls (job-step notifications) for the subsystem name through ufsdssir, which rejects them. This is expected. After `/P UFSD` (which deregisters the SSCT), `S UFSD` succeeds again.

**Danger case:** If UFSD abends without cleanup, the SSCT remains registered until IPL. **ESTAE exit is mandatory** (see section 4.8).

#### 4.2.2 IEFSSNxx Registration (optional, later phase)

Static registration via `SYS1.PARMLIB(IEFSSNxx)`. MVS 3.8j compatibility must be verified before adoption. Programmatic registration (4.2.1) remains the safe path.

### 4.3 SSI Router (ufsdssir)

Runs in the **client address space**. Contains **zero business logic**. Declared as `void ufsdssir(void)` — MVS SSI convention passes R1=SSOB directly, not as a C parameter list.

**State transitions:**

```
Entry: supervisor state, key 0 (set by IEFSSREQ)
  │
  ├─ Extract R1=SSOB via inline asm
  ├─ Validate SSOB eye catcher
  ├─ Allocate request from CSA free pool
  ├─ Copy parameters into request
  ├─ CS lock-free enqueue
  ├─ __xmpost(server_ascb, &server_ecb, 0)  ← supervisor state
  │
  ├─ __prob                                   ← switch to problem state
  │
  ├─ WAIT(1, &local_ecb)                     ← problem state, key-8 ECB
  │
  ├─ Copy results back to caller
  ├─ Free request block
  └─ Return
```

```c
/* SSI Thin Router — ZERO business logic */
/* Entry: R1 = SSOB pointer (MVS convention, NOT C plist) */
void ufsdssir(void) {
    SSOB *ssob;
    ECB   local_ecb = 0;  /* key-8, on client's stack */

    /* Extract SSOB from R1 before compiler touches plist */
    __asm__ __volatile__("LR %0,1" : "=r"(ssob));

    UFSD_ANCHOR *anchor = get_ufsd_anchor();
    UFSSSOB *ussob = (UFSSSOB *)ssob->ssobindv;

    /* Validate */
    if (memcmp(ssob->ssobid, "SSOB", 4) != 0) {
        ussob->rc = UFSD_RC_BADSSOB;
        return;
    }

    UFSREQ *req = ufsd_alloc_request(anchor);
    if (!req) {
        ussob->rc = UFSD_RC_NOREQ;
        return;
    }

    /* Fill request (supervisor state, can write key-0 CSA) */
    req->func          = ussob->function;
    req->session_token = ussob->session;
    req->data_len      = ussob->parmlen;
    req->client_ecb_ptr = &local_ecb;      /* points to client stack */
    req->client_ascb   = __ascb(0);         /* client's ASCB */
    memcpy(req->data, ussob->parmdata,
           MIN(ussob->parmlen, UFSREQ_MAX_INLINE));

    /* Lock-free enqueue */
    int was_empty = ufsd_enqueue(anchor, req);

    /* Wake STC via cross-AS POST (supervisor state) */
    if (was_empty)
        __xmpost(anchor->server_ascb, &anchor->server_ecb, 0);

    /* Switch to problem state for WAIT */
    __prob();

    /* WAIT on local ECB (key-8, client stack) */
    __wait1(&local_ecb);

    /* Copy results back */
    ussob->rc        = req->rc;
    ussob->errno_val = req->errno_val;
    ussob->data_len  = req->data_len;
    memcpy(ussob->parmdata, req->data, req->data_len);

    ufsd_free_request(anchor, req);
}
```

#### 4.3.1 SSI Residency Strategy

| Variant       | Mechanism                                    | Use Case                     |
|---------------|----------------------------------------------|------------------------------|
| **Development** | GETMAIN SP=241 + LOAD into CSA. No IPL.    | Iterative dev on Hercules.   |
| **Production**  | IEALPAxx, module in SYS1.LPALIB. IPL needed. | Stable ops, no CSA for code. |

### 4.4 CSA Request Queue

#### 4.4.1 Anchor Block

```c
typedef struct ufsd_anchor {
    char     eye[8];        /* "UFSDANCR"              */
    UINT32   version;
    UINT32   flags;
#define UFSD_ACTIVE   0x80000000
#define UFSD_QUIESCE  0x40000000
    ECB      server_ecb;    /* STC WAITs on this (key-0) */
    void     *server_ascb;  /* STC's ASCB for __xmpost   */

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

    /* Session + Global File Tables (STC-local pointers) */
    void     *sessions;     /* -> UFSD_SESSION array     */
    UINT32   max_sessions;
    void     *gfiles;       /* -> UFSD_GFILE array       */
    UINT32   max_gfiles;

    /* SSCT (for deregistration) */
    void     *ssct;         /* -> SSCT in CSA            */

    /* Trace Ring Buffer */
    UFSD_TRACE *trace_buf;
    UINT32   trace_next;
    UINT32   trace_size;

    /* Statistics */
    UINT32   stat_requests;
    UINT32   stat_errors;
    UINT32   stat_posts_saved;
} UFSD_ANCHOR;
```

#### 4.4.2 Request Block

```c
typedef struct ufsreq {
    char     eye[8];        /* "UFSREQ__"              */
    UFSREQ   *next;         /* chain pointer           */
    ECB      *client_ecb_ptr; /* -> local ECB on client stack (key-8) */
    void     *client_ascb;  /* client's ASCB for __xmpost */
    UINT32   func;          /* function code           */
    UINT32   session_token;
    INT32    rc;
    INT32    errno_val;
    UINT32   data_len;
    UFSBUF   *buf;          /* -> 4K buffer (or NULL)  */
    char     data[256];     /* inline parm area        */
} UFSREQ;  /* ~296 bytes */
```

**Key change from Concept #4:** `client_ecb` is no longer an ECB field in the request block. It is a **pointer** to a local ECB on the client's stack (key-8 storage). This avoids X'201' (WAIT on key-0 ECB from problem state). `client_ascb` is needed by `__xmpost` for cross-AS POST.

#### 4.4.3 Inline-Buffer Optimization

| Operation             | Typical Size | Buffer Strategy      |
|-----------------------|--------------|----------------------|
| fopen (path + mode)   | ~140 bytes   | **Inline**           |
| mkdir/chgdir/rmdir    | ~130 bytes   | **Inline**           |
| dirread (1 entry)     | ~80 bytes    | **Inline**           |
| fread/fwrite (<256 B) | <256 bytes   | **Inline**           |
| fread/fwrite (>256 B) | up to 4096   | **4K Pool Buffer**   |
| fread/fwrite (>4K)    | >4096 bytes  | **Chunked**, 4K each |

>80% of all requests fit inline. The 4K pool is primarily for bulk data transfer.

#### 4.4.4 Lock-Free Enqueue with POST Bundling

```c
int ufsd_enqueue(UFSD_ANCHOR *anchor, UFSREQ *req) {
    UFSREQ *old_tail;
    req->next = NULL;
    do {
        old_tail = anchor->req_tail;
    } while (__cs(&old_tail, &anchor->req_tail, req) != 0);
    if (old_tail) {
        old_tail->next = req;
        return 0;  /* queue was not empty */
    } else {
        anchor->req_head = req;
        return 1;  /* queue was empty, POST needed */
    }
}
```

Conditional POST saves SVC overhead under load. `stat_posts_saved` tracks savings.

### 4.5 Request Validation

```c
int ufsd_validate_request(UFSREQ *req) {
    if (memcmp(req->eye, "UFSREQ__", 8) != 0)
        return UFSD_RC_CORRUPT;
    if (req->func < UFSREQ_MIN || req->func > UFSREQ_MAX)
        return UFSD_RC_BADFUNC;
    if (req->func != UFSREQ_SESS_OPEN &&
        !ufsd_session_valid(req->session_token))
        return UFSD_RC_BADSESS;
    return 0;
}
```

### 4.6 Diagnostic Trace Ring Buffer

```c
typedef struct ufsd_trace_entry {
    UINT32   timestamp;     /* STCK low-word         */
    UINT16   func;          /* function code         */
    UINT16   rc;            /* return code           */
    UINT32   session_token;
    UINT32   asid;
} UFSD_TRACE;  /* 16 bytes per entry */
```

Standard: 256 entries = 4K. In ABEND dump: find anchor via "UFSDANCR", read `trace_buf` + `trace_next`, walk backwards.

### 4.7 CSA Memory Budget

| Resource              | Count | Size/each  | CSA Total |
|-----------------------|-------|------------|-----------|
| UFSD Anchor           | 1     | ~512 B     | ~1K       |
| Request-Block Pool    | 32    | ~296 B     | ~10K      |
| 4K Data Buffer Pool   | 16    | 4096 B     | ~64K      |
| Trace Ring Buffer     | 256   | 16 B       | ~4K       |
| SSCT + SSCVT          | 1+1   | ~64 B      | ~1K       |
| SSI Routine (dev)     | 1     | ~4K        | ~4K       |
| **Total CSA**         |       |            | **~84K**  |

Note: Session table and Global File Table are in STC heap, not CSA. Reduced from Concept #4's ~101K.

### 4.8 ESTAE Abend Recovery

**Mandatory.** Without ESTAE, an abend in the STC leaves the SSCT registered. Subsequent `S UFSD` fails with IEF612I until IPL.

```c
/* In ufsd.c main() — before main loop */
abendrpt(ESTAE_CREATE, DUMP_DEFAULT);

/* ESTAE retry routine calls: */
void ufsd_estae_cleanup(UFSD_ANCHOR *anchor) {
    ufsd_deregister_ssct(anchor);
    ufsd_free_csa(anchor);
    wtof("UFSD098I UFSD abend recovery: SSCT deregistered, CSA freed");
}
```

## 5. Multi-Client File Descriptor Model

Two-level model:

```
Client fd → Session fd_table[64] → Global Open File → Inode
```

### 5.1 Data Structures

#### Session (STC heap)

```c
typedef struct ufsd_session {
    char     eye[8];         /* "UFSDSESS"            */
    UINT32   token;          /* ((slot+1)<<16)|(serial&0xFFFF) */
    UINT32   client_asid;
    UINT32   flags;
#define UFSD_SESS_ACTIVE  0x80000000
#define UFSD_SESS_CLEANUP 0x40000000
    UFSD_UFS *ufs;           /* server-side UFS handle */
    UFSD_FD  fd_table[UFSD_MAX_FD];
} UFSD_SESSION;

#define UFSD_MAX_FD  64
```

Token scheme: `((slot+1) << 16) | (serial & 0xFFFF)`. Slot extracted as `(token >> 16) - 1`. Serial prevents stale-token reuse.

#### Per-Session File Descriptor

```c
typedef struct ufsd_fd {
    UINT32   gfile_idx;
#define UFSD_FD_UNUSED  0xFFFFFFFF
    UINT32   flags;
#define UFSD_FD_READ    0x80000000
#define UFSD_FD_WRITE   0x40000000
} UFSD_FD;
```

#### Global Open File Entry (STC heap)

```c
typedef struct ufsd_gfile {
    char     eye[8];         /* "UFSDGFIL"            */
    UINT32   flags;
#define UFSD_GF_USED    0x80000000
    UINT32   inode_num;      /* inode number           */
    UINT32   disk_idx;       /* which UFSD_DISK        */
    UINT32   position;       /* file offset            */
    UINT32   open_mode;
    UINT32   refcount;
} UFSD_GFILE;
```

**No shared position:** MVS has no fork(). Every fopen() creates an independent Global File Entry. Refcount is always 1 in current implementation.

### 5.2 Session Lifecycle

**Create:** Find free slot → generate token → allocate UFSD_UFS handle (cwd="/") → return token.

**Close:** Close all open fds → free UFSD_UFS → release slot.

**ABEND cleanup (ASID scan):** For each active session, check if client ASCB still exists. If terminated: close all fds, free session, trace UFSD_T_SESS_ABEND.

## 6. Operator Command Interface

Commands via `/F UFSD,<command>` using CIB/QEDIT:

| Command                       | Description                                    |
|-------------------------------|------------------------------------------------|
| `/F UFSD,MOUNT DD=x,PATH=y`  | Mount BDAM dataset on path                     |
| `/F UFSD,UNMOUNT PATH=y`     | Unmount path                                   |
| `/F UFSD,STATS`              | Statistics                                     |
| `/F UFSD,SESSIONS`           | List active sessions                           |
| `/F UFSD,TRACE ON\|OFF\|DUMP`| Control trace ring buffer                      |
| `/F UFSD,SHUTDOWN`           | Orderly shutdown                               |
| `/P UFSD`                    | Standard MVS STOP                              |

### Main Loop

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
            /* Wake client via cross-AS POST (supervisor state) */
            __super();
            __xmpost(req->client_ascb, req->client_ecb_ptr, 0);
            __prob();
        }
    }
    ufsd_shutdown(anchor);
}
```

## 7. Dispatch Model

### Phase 1: Single-Threaded (current)

Request processing and command handling in the same thread. A slow BDAM I/O blocks all waiting clients. Acceptable for PoC.

### Target: Multi-Worker via ATTACH (Phase 3)

Pre-ATTACHed worker pool (4–8). Main loop dequeues and assigns to free workers. Workers dispatch and POST completion. Prerequisite: inode R/W locking.

## 8. On-Disk Structure Constants

Derived from `ufs370/include/ufs/disk.h`:

| Constant | Value | Notes |
|----------|-------|-------|
| `UFSD_ROOT_INO` | 2 | Root directory inode |
| `UFSD_INODE_SIZE` | 128 | Bytes per on-disk inode |
| `UFSD_INODES_PER_BLK` | 32 | For 4096-byte block |
| `UFSD_DIRENT_SIZE` | 64 | Bytes per directory entry |
| `UFSD_DIRENTS_PER_BLK` | 64 | For 4096-byte block |
| `UFSD_NADDR_DIRECT` | 16 | Direct block addresses per inode |

Inode-to-sector: `sector = sb->ilist_start + (ino-1)/32`, `offset = ((ino-1)%32) * 128`

## 9. Implemented Source Modules

| Module | File | Description |
|--------|------|-------------|
| UFSD | `src/ufsd.c` | STC main, main loop, shutdown |
| UFSD#CMD | `src/ufsd#cmd.c` | MODIFY command parser |
| UFSD#CSA | `src/ufsd#csa.c` | CSA allocation, pool management |
| UFSD#SSC | `src/ufsd#ssc.c` | SSCT/SSCVT registration |
| UFSDSSIR | `src/ufsdssir.c` | SSI Thin Router |
| UFSD#QUE | `src/ufsd#que.c` | Lock-free queue, dispatch routing |
| UFSD#SES | `src/ufsd#ses.c` | Session management |
| UFSD#INI | `src/ufsd#ini.c` | Disk init (TIOT scan, BDAM open) |
| UFSD#BLK | `src/ufsd#blk.c` | BDAM block read/write |
| UFSD#SBL | `src/ufsd#sbl.c` | Superblock I/O, block/inode alloc |
| UFSD#INO | `src/ufsd#ino.c` | Inode I/O, direct block addressing |
| UFSD#DIR | `src/ufsd#dir.c` | Directory lookup/add/remove |
| UFSD#FIL | `src/ufsd#fil.c` | File operation dispatch |
| UFSD#GFT | `src/ufsd#gft.c` | Global File Table |
| UFSD#TRC | `src/ufsd#trc.c` | Trace ring buffer |

Client programs: `client/ufsdping.c` (Ping/Pong), `client/ufsdtst.c` (integration test).

## 10. Current Deferrals (Phase 1 PoC)

| Item | Deferred to | Reason |
|------|-------------|--------|
| Indirect blocks (single/double/triple) | post-AP-1f | Direct blocks cover 64K per file, sufficient for PoC |
| Permission / ACEE checking | post-AP-1f | No RACF integration in Phase 1 |
| Timestamps (atime/mtime/ctime) | post-AP-1f | Fields present, just not filled |
| Superblock freeblock cache refill | post-AP-1f | Pre-populated by mkufs, NOSPACE on exhaustion |
| Inode cache / pager layer | post-AP-1f | Direct BDAM I/O, acceptable for single-client |
| diropen/dirread/dirclose | post-AP-1f | Not in AP-1e test cycle |
| fseek | post-AP-1f | Sequential access only in tests |

## 11. Future Direction: VFS Abstraction (Phase 4+)

A full VFS layer could introduce `ufs_vfs_ops` function pointers for multiple filesystem types (RAMFS, DEVFS) behind the same path namespace. UFS370's existing `UFSIO` abstraction provides an anchor point. Not part of current design.

## 12. Phase Plan

| Phase | Scope | Deliverables |
|-------|-------|--------------|
| **1** | STC + CSA PoC | STC, SSCT, CSA queue (CS + POST bundling + __xmpost), SSI router, sessions, fd_table, file ops, trace, MODIFY commands, ESTAE |
| **2** | Stubs + Integration | libufs client stubs (~30 functions), HTTPD/FTPD integration, MOUNT/UNMOUNT commands |
| **3** | Multi-Worker + Concurrency | Pre-ATTACHed workers, inode/dir locking, ETXR, SSI to LPA, stress tests |
| **4+** | Future | VFS abstraction, indirect blocks, RACF, pager/cache, performance |
