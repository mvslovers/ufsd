# UFS370 Phase 1 — Work Packages

Proof of Concept — Detailed Plan  
Based on Concept #4

---

## 1. Overview

Phase 1 (STC + CSA Proof of Concept) is broken into six sequential work packages. Each has a clear deliverable and is independently testable on Hercules.

### 1.1 Dependency Graph

```
AP-1a  STC Skeleton
  │
  ▼
AP-1b  CSA Infrastructure + SSCT
  │
  ▼
AP-1c  SSI Router + First Round-Trip
  │
  ▼
AP-1d  Session Management
  │
  ▼
AP-1e  File Operations + fd_table
  │
  ▼
AP-1f  Integration + HTTPD Binding
```

Strictly sequential: each WP builds on the previous one.

### 1.2 Summary Table

| WP     | Title           | Core Deliverable              | Test Criterion          |
|--------|-----------------|-------------------------------|-------------------------|
| **1a** | STC Skeleton    | Running STC with MODIFY/STOP  | S/F/P on console        |
| **1b** | CSA + SSCT      | Anchor + pools in CSA, SSCT   | Dump analysis           |
| **1c** | SSI Round-Trip  | Cross-AS Ping/Pong            | Client pgm receives Pong|
| **1d** | Sessions        | Session Open/Close + UFS init | STATS shows session     |
| **1e** | File Operations | fopen/fread/fclose via SSI    | Read file cross-AS      |
| **1f** | Integration     | libufs stubs + HTTPD test     | HTTPD serves from UFS   |

---

## AP-1a: STC Skeleton

**Dependencies:** None (start package)

**Goal:** A minimal STC that starts on Hercules/MVS, accepts operator commands, and shuts down cleanly. No UFS, no CSA, no subsystem yet.

### Scope

- STC main program (UFSD) as endless loop with WAIT
- QEDIT ORIGIN for CIB reception (MODIFY/STOP)
- Command parser for: STATS, SHUTDOWN, HELP
- STOP (`/P UFSD`) sets QUIESCE flag, loop exits cleanly
- WTO messages with UFSD prefix and message numbers
- JCL proc for the STC

### Deliverables

| Artifact         | Description                                              |
|------------------|----------------------------------------------------------|
| `UFSD`           | Load module for STC (C, linked with CRENT370)            |
| `UFSD JCL Proc`  | STC proc in SYS1.PROCLIB (DD cards prepared but empty)  |

### Test Criteria

```
/S UFSD
UFSD000I UFSD Filesystem Server V0.1 starting
UFSD001I UFSD ready

/F UFSD,STATS
UFSD010I UFSD STATUS: ACTIVE

/F UFSD,HELP
UFSD020I Commands: STATS, SHUTDOWN, HELP

/P UFSD
UFSD099I UFSD shutdown complete
```

### Code Sketch

```c
int main(int argc, char **argv) {
    UFSD ufsd = {0};
    strcpy(ufsd.eye, "**UFSD**");
    ufsd.flags = UFSD_ACTIVE;

    /* Enable MODIFY/STOP commands */
    IEZCOM *com = setup_qedit();

    wtof("UFSD000I UFSD Filesystem Server starting");

    while (ufsd.flags & UFSD_ACTIVE) {
        WAIT(1, &ufsd.wait_ecb);
        ufsd.wait_ecb = 0;

        ufsd_check_commands(&ufsd, com);
    }

    ufsd_shutdown(&ufsd);
    return 0;
}
```

Deliberately minimal. No CSA allocation, no UFS. Just prove the STC mechanics work. Use the HTTPD codebase (`httpd.c`, `httpcmd.c`) as reference for CIB/QEDIT patterns.

---

## AP-1b: CSA Infrastructure + SSCT

**Dependencies:** AP-1a

**Goal:** Allocate CSA structures (anchor, request pool, buffer pool, trace buffer), register SSCT in JESCT chain, clean up everything on STOP.

### Scope

- GETMAIN SP=241 for UFSD_ANCHOR (~512 bytes)
- Request block pool: 32 × 304 bytes pre-allocated, free chain
- 4K buffer pool: 16 × 4096 bytes, free chain
- Trace ring buffer: 256 × 16 bytes = 4K
- SSCT + SSCVT allocated in CSA, chained into JESCT
- SSI function routine address set to **dummy** (returns RC=8 "not yet implemented")
- On STOP: deregister SSCT, FREEMAIN all CSA blocks
- STATS command extended: shows CSA allocations

### Deliverables

| Artifact        | Description                                               |
|-----------------|-----------------------------------------------------------|
| `ufsd_csa.c`    | CSA allocation, pool init, free chain management          |
| `ufsd_ssct.c`   | SSCT/SSCVT registration and deregistration                |
| `ufsd_trace.c`  | Trace ring buffer write + dump function                   |
| `ufsd.h`        | All control block definitions (UFSD_ANCHOR, UFSREQ, etc.) |

### Test Criteria

```
/S UFSD
UFSD000I UFSD Filesystem Server V0.2 starting
UFSD030I CSA allocated: Anchor=00FE4000
UFSD031I   Request Pool: 32 blocks, 10K
UFSD032I   Buffer Pool:  16 blocks, 64K
UFSD033I   Trace Buffer: 256 entries, 4K
UFSD034I SSCT registered, subsystem name=UFSD
UFSD001I UFSD ready

/F UFSD,STATS
UFSD010I STATUS: ACTIVE
UFSD011I CSA FREE REQS:  32/32
UFSD012I CSA FREE BUFS:  16/16
UFSD013I TRACE ENTRIES:  256

/P UFSD
UFSD095I SSCT deregistered
UFSD096I CSA freed
UFSD099I UFSD shutdown complete
```

**Dump test:** After start, trigger SLIP TRAP. In the dump, find anchor via eye catcher "UFSDANCR" in CSA. All pools must have correct eye catchers.

---

## AP-1c: SSI Router + First Round-Trip

**Dependencies:** AP-1b

**Goal:** First successful cross-address-space request via IEFSSREQ. A client test program sends PING, the STC responds with PONG.

### Scope

- **SSI Thin Router:** Compile, load as module into CSA (GETMAIN + LOAD). Only enqueue/POST/WAIT, no logic.
- **CS lock-free enqueue:** `ufsd_enqueue()` with conditional POST
- **Server dequeue + dispatch:** Main loop receives requests, dispatch stub writes RC=0 back, POSTs client ECB
- **Client test program UFSDPING:** Batch program, builds SSOB, calls IEFSSREQ, outputs result via WTO
- **Request validation:** Eye catcher + function code check in dispatch
- **Trace:** Every request/response recorded in trace buffer

### Deliverables

| Artifact          | Description                                            |
|-------------------|--------------------------------------------------------|
| `ufsd_ssi.c`      | SSI Thin Router (loaded into CSA)                     |
| `ufsd_queue.c`    | Lock-free enqueue (CS), dequeue, alloc/free request   |
| `ufsd_dispatch.c` | Request validation + dispatch stub                    |
| `UFSDPING`        | Client test program (batch)                           |

### Test Criteria

```
/* STC running */
/S UFSD

/* Batch job with UFSDPING */
//PING  EXEC PGM=UFSDPING

/* Client output: */
UFSPING1I Sending PING to UFSD subsystem...
UFSPING2I UFSD responded: RC=0 (PONG)
UFSPING3I Round-trip time: ~XXX microseconds

/* STC console: */
/F UFSD,STATS
UFSD014I REQUESTS SERVED:     1
UFSD015I ERRORS:              0
```

**This is the critical milestone.** If ping/pong works, the entire cross-AS communication is proven: SSCT discovery, IEFSSREQ, CSA queue, POST/WAIT, dispatch, response. If it fails, evaluate fallback (direct CSA communication without SSI).

---

## AP-1d: Session Management

**Dependencies:** AP-1c

**Goal:** Clients can open and close sessions. The STC initializes the UFS filesystem and manages session state.

### Scope

- **UFS init in STC:** `ufs_sys_new()` at STC startup. DD cards UFSDISK0–9 in STC JCL proc.
- **Session Create (UFSREQ_SESS_OPEN):** Allocates session slot, generates token, calls `ufsnew()` server-side
- **Session Close (UFSREQ_SESS_CLOSE):** Releases session, calls `ufsfree()`
- **Session Table:** Array in STC address space (not CSA), max 64 sessions
- **Token validation:** Every request validates session token
- **ASID scan:** Periodic check (on each WAIT timeout or MODIFY) whether session ASIDs still have active ASCBs
- **MODIFY SESSIONS:** Shows active sessions on console
- **Client UFSDTEST:** Extension of UFSDPING — session open, session close

### Deliverables

| Artifact          | Description                                            |
|-------------------|--------------------------------------------------------|
| `ufsd_session.c`  | Session create/close/validate/cleanup                  |
| `ufsd_init.c`     | UFS initialization in STC (ufs_sys_new + fstab)       |
| `UFSD JCL Proc`   | Extended with UFSDISK0–9 DD cards                     |
| `UFSDTEST`        | Client test program with session lifecycle             |

### Test Criteria

```
/S UFSD
UFSD040I UFS initialized, 2 disks mounted
UFSD041I   UFSDISK0 DD=UFSDISK0 DSN=UFS.ROOT   (root)
UFSD042I   UFSDISK1 DD=UFSDISK1 DSN=UFS.DATA
UFSD043I   /etc/fstab: mounted UFSDISK1 on /data

/* Client job UFSDTEST */
UFSTST01I Session opened, token=0x00010041
UFSTST02I Session closed

/F UFSD,SESSIONS
UFSD050I ACTIVE SESSIONS: 0

/* While client is running: */
/F UFSD,SESSIONS
UFSD050I ACTIVE SESSIONS: 1
UFSD051I   #1 TOKEN=00010041 ASID=0023 FDs=0/64
```

---

## AP-1e: File Operations + fd_table

**Dependencies:** AP-1d

**Goal:** Clients can open, read, write and close files via IEFSSREQ. The fd_table and Global Open File Table are functional.

### Scope

- **Per-session fd_table[64]** + **Global Open File Table** (256 entries)
- **Dispatch functions:** FOPEN, FCLOSE, FREAD, FWRITE, FSEEK, MKDIR, CHGDIR, RMDIR, REMOVE, DIROPEN, DIRREAD, DIRCLOSE
- **Request/response marshalling:** Parameter serialization for paths, modes, data
- **Inline buffer:** Small requests/responses via data[256]
- **4K buffer:** Large reads/writes via pool buffer, chunking in client
- **MODIFY STATS extended:** Shows open files, fd usage
- **Client UFSDTEST extended:** Session open, mkdir, create file, write, read, verify, delete, session close

### AP-1e Simplifications (PoC Scope — intentional deferrals)

These items are deliberately out of scope for the Phase 1 PoC. They are listed here
so they are not forgotten. Each will be addressed in a follow-up phase or AP-1f.

| # | Item | Deferred to | Reason |
|---|------|-------------|--------|
| 1 | **Indirect blocks** (addr[16] single, addr[17] double, addr[18] triple) | post-AP-1f | Test file is 13 bytes; direct blocks (addr[0..15]) cover 64 KB per file, which is sufficient for the entire PoC. |
| 2 | **Permission / ACEE checking** | post-AP-1f | No RACF integration in Phase 1. All clients are trusted (APF-auth). |
| 3 | **Timestamps** (atime, mtime, ctime) | post-AP-1f | Written as 0 on create/modify. UFS inode fields are present; we just do not fill them. |
| 4 | **Superblock freeblock cache refill** | post-AP-1f | On a freshly formatted UFS disk the freeblock cache is pre-populated by `ufs370/tools/mkufs`. If `sb->nfreeblock == 0`, return `UFSD_RC_NOSPACE` — no cache-refill walk needed for the PoC test. |
| 5 | **Inode cache / pager layer** | post-AP-1f | Every inode read/write goes directly to BDAM. No in-memory inode table. Acceptable for single-client PoC; revisit for Phase 3 (multi-worker). |
| 6 | **diropen / dirread / dirclose** | post-AP-1f | Not exercised in the AP-1e test cycle. Directory listing is deferred. |
| 7 | **fseek** | post-AP-1f | Not used in the AP-1e test cycle (sequential write then sequential read). |

### On-disk Structure Constants (AP-1e)

Derived from `ufs370/include/ufs/disk.h`:

| Constant | Value | Notes |
|----------|-------|-------|
| `UFSD_ROOT_INO` | 2 | Inode 1 = BALBLK (reserved by UFS format); root dir = inode 2 |
| `UFSD_INODE_SIZE` | 128 | bytes per on-disk inode |
| `UFSD_INODES_PER_BLK` | 32 | for 4096-byte block: 4096/128 |
| `UFSD_DIRENT_SIZE` | 64 | bytes per directory entry |
| `UFSD_DIRENTS_PER_BLK` | 64 | for 4096-byte block: 4096/64 |
| `UFSD_SB_OFFSET` | 512 | superblock starts at byte 512 within sector 1 |
| `UFSD_SB_SIZE` | 512 | superblock is 512 bytes |

Inode-to-sector mapping (ilist starts at sector given by `sb->ilist_start`):

```
sector = sb->ilist_start + (ino - 1) / UFSD_INODES_PER_BLK
offset = ((ino - 1) % UFSD_INODES_PER_BLK) * UFSD_INODE_SIZE
```

### New Source Modules (AP-1e)

| Module | Source File | Description |
|--------|-------------|-------------|
| `UFSD#BLK` | `src/ufsd#blk.c` | BDAM block read/write (sector-addressed, uses `disk->dcb`) |
| `UFSD#SBL` | `src/ufsd#sbl.c` | Superblock I/O, freeblock alloc/free, freeinode alloc/free |
| `UFSD#INO` | `src/ufsd#ino.c` | Inode I/O + block address resolution (direct blocks only) |
| `UFSD#DIR` | `src/ufsd#dir.c` | Directory lookup/add/remove, path resolution (`/a/b/c`) |
| `UFSD#FIL` | `src/ufsd#fil.c` | FOPEN/FCLOSE/FREAD/FWRITE/MKDIR/RMDIR/CHGDIR/REMOVE dispatch |
| `UFSD#GFT` | `src/ufsd#gft.c` | Global File Table init/alloc/free |

### Deliverables

| Artifact          | Description                                            |
|-------------------|--------------------------------------------------------|
| `ufsd#blk.c`      | BDAM block read/write                                  |
| `ufsd#sbl.c`      | Superblock management + block/inode allocation         |
| `ufsd#ino.c`      | Inode read/write + direct block address resolution     |
| `ufsd#dir.c`      | Directory lookup, path walk, entry add/remove          |
| `ufsd#fil.c`      | File operation dispatch (FOPEN .. REMOVE)              |
| `ufsd#gft.c`      | Global Open File Table (256 entries, STC heap)         |
| `UFSDTEST`        | Extended integration test (create/write/read/delete)   |

### Test Criteria

```
/* Client job UFSDTEST — full cycle */
UFSTST01I Session opened
UFSTST10I mkdir /test → OK
UFSTST11I chdir /test → OK
UFSTST12I fopen /test/hello.txt (write) → fd=0
UFSTST13I fwrite 13 bytes → OK
UFSTST14I fclose fd=0 → OK
UFSTST15I fopen /test/hello.txt (read) → fd=0
UFSTST16I fread 13 bytes → "Hello, UFS!\n"
UFSTST17I fclose fd=0 → OK
UFSTST18I Data verified OK
UFSTST19I remove /test/hello.txt → OK
UFSTST20I rmdir /test → OK
UFSTST02I Session closed
```

---

## AP-1f: Integration + HTTPD Binding

**Dependencies:** AP-1e

**Goal:** API-compatible client stub library (libufs) that transparently replaces existing UFS API calls with SSI requests. HTTPD uses the UFSD subsystem instead of local UFS initialization.

### Scope

- **libufs client stub library:** ~30 stub functions, API-identical to existing UFS functions. Each call transparently wraps an IEFSSREQ call.
- **HTTPD modification:** In `httpconf.c/process_httpd_ufs()`: instead of `ufs_sys_new()` + `ufsnew()` locally, send `UFSREQ_SESS_OPEN` to UFSD STC. The rest (fopen/fread/fclose in httpopen.c/httpfile.c) works transparently through the stub library.
- **FTPD modification:** Analogous — the FTPD currently shares `httpd->ufssys`. With the STC it gets its own session.
- **MODIFY MOUNT/UNMOUNT:** Dynamic mount/unmount at runtime
- **MODIFY TRACE DUMP:** Outputs last N trace entries to console

### Deliverables

| Artifact          | Description                                            |
|-------------------|--------------------------------------------------------|
| `libufs (NCALIB)` | Client stub library with ~30 API-compatible functions |
| `HTTPD Patch`     | Minimal patch for httpconf.c: UFS init via UFSD session|
| `ufsd_mount.c`    | MODIFY MOUNT/UNMOUNT command handler                   |

### Test Criteria

```
/* Both STCs running */
/S UFSD
/S HTTPD

/* HTTPD opens session to UFSD STC */
HTTPD046I UFS session opened via UFSD subsystem

/* Browser access to static UFS file */
http://mvs:8080/index.html  → 200 OK

/* FTP access to UFS file */
ftp mvs 8021 → ls / → directory listing

/* Dynamic mount */
/F UFSD,MOUNT DD=UFSDISK2,PATH=/extra
UFSD060I Mounted UFSDISK2 on /extra

/* Concurrent access */
HTTPD + FTPD + batch job UFSDTEST all in parallel
→ No errors, STATS shows 3 sessions
```

**Phase 1 acceptance test:** HTTPD serves an HTML page from UFS via the UFSD STC while a batch job simultaneously creates and reads files via UFSDTEST. No errors, STATS shows correct session and file counts.

---

## 4. Current Implementation Status

_Last updated: AP-1c debugging_

### AP-1a — DONE
STC skeleton, MODIFY/STOP, WTO messages, JCL proc. All test criteria pass.

### AP-1b — DONE
CSA anchor, request pool, buffer pool, trace ring, SSCT registration, STATS command.
Clean shutdown (SSCT deregistered, CSA freed) confirmed via `/P UFSD`.

### AP-1c — DONE ✓

**Milestone achieved:** `UFSPING2I UFSD responded: RC=0 (PONG)` — CC 0000 confirmed.

The road to a working cross-AS round-trip required root-causing five successive abends.
Each one is documented here as a permanent reference for MVS 3.8j cross-AS mechanics.

---

#### Abend 1: S047 (WAIT protection error)

**Symptom:** UFSDSSIR called `ecb_wait()` on `anchor->server_ecb` (in CSA, key 0).
An unauthorised task cannot WAIT on a key-0 ECB → S047.

**Fix:** Use a client-private ECB (`ECB ping_ecb` in ufsdping.c, key 8).
UFSDSSIR WAITs on the client's own ECB via inline `WAIT ECB=(%0)`.
Added `clib_apf_setup(argv[0])` to ufsdping.c (needed by crent370's iefssreq for
`MODESET KEY=ZERO` internally).

---

#### Abend 2: S0C4 / INTC=0x0010 (segment translation at entry to ufsdssir)

**Root cause:** crent370's `iefssreq` passes R1 = SSOB address directly to the
SSVT routine (MVS convention). c2asm370's C calling convention expects R1 = pointer
to parameter list. Declaring `ufsdssir(SSOB *ssob)` caused the compiler to dereference
the raw SSOB address as a C plist → crash in `memcmp`.

Also: `UFSSSOB.client_ecb` was placed at offset +12, which is where `iefssreq.c`
reads the "SSOB individual block pointer" to pass as R1 — so `iefssreq` handed
`&ping_ecb` to UFSDSSIR as the SSOB address.

**Fix:** Declared `void ufsdssir(void)`. Extract SSOB from R1 via inline asm
before any compiler-generated plist dereference:
```c
__asm__ __volatile__("LR %0,1" : "=r"(ssob));
```
Moved `UFSSSOB.client_ecb` to end of struct (after `data[]`).

---

#### Abend 3: S202 (SVC 2 POST from supervisor state)

**Symptom:** `ecb_post(&anchor->server_ecb, 0)` was called from within a
`__super/__prob` block (supervisor state). POST (SVC 2) from supervisor state → S202.

**Fix:** Split the supervisor-state window. Call `__prob` first, then `ecb_post`
from problem state.

---

#### Abend 4: S102 (cross-AS POST via SVC 2)

**Symptom:** `ecb_post` (SVC 2) from problem state for cross-address-space POST
→ S102. Affects both directions: ufsdssir→STC and STC→ufsdssir.

**Root cause:** SVC 2 (POST) cannot post an ECB whose waiting task is in a different
address space from problem state without special cross-memory authority.

**Fix:** Replace `ecb_post` with `__xmpost` (CVT0PT01 branch entry) in both places:
- ufsdssir wakes STC: `__xmpost(anchor->server_ascb, &anchor->server_ecb, 0)`
  from supervisor state (still in the Phase-1 key-0 window, before `__prob`).
- STC wakes client: `__xmpost(req->client_ascb, req->client_ecb_ptr, 0)`
  from supervisor state inside the result-write key-0 window.

Added `void *server_ascb` to `UFSD_ANCHOR`; set at STC startup via `__ascb(0)`.
`req->client_ascb` set in ufsdssir via `__ascb(0)` (captures client ASCB).

---

#### Abend 5: X'201' (WAIT from problem state on key-0 storage)

**Symptom:** After fixing cross-AS POST, UFSDPING abended X'201'. Dump analysis:
- PSW at abend: problem state, key 8
- ECB address in SVRB R1: `00A70954` = `req->client_ecb` in CSA (SP=241, key 0)
- X'201' = "WAIT (SVC 1) issued from problem state for an ECB in key-0 storage"

**Root cause:** The previous design stored the completion ECB inside the UFSREQ
block (CSA, SP=241, storage key 0). WAIT from problem state on a key-0 ECB is
not permitted → X'201'.

**Fix:** Remove `ECB client_ecb` from `UFSREQ`. Declare `ECB local_ecb` as a
**local stack variable** in ufsdssir (key-8 storage, in the client's address space).
Store `req->client_ecb_ptr = &local_ecb`. WAIT from problem state on `&local_ecb`
(key 8) works correctly. `__xmpost` (CVT0PT01) posts it cross-AS from the STC.

---

#### Final working design (key invariants)

| Operation | Mechanism | State | Notes |
|-----------|-----------|-------|-------|
| ufsdssir wakes STC | `__xmpost(server_ascb, &server_ecb, 0)` | supervisor | before `__prob` |
| ufsdssir WAITs for reply | `WAIT ECB=(&local_ecb)` | problem | key-8 local ECB |
| STC wakes client | `__xmpost(client_ascb, client_ecb_ptr, 0)` | supervisor | inside key-0 window |
| client_ecb location | local stack var in ufsdssir | key-8 | NOT in CSA |
| server_ecb location | `anchor->server_ecb` in CSA | key-0 | STC WAITs in supervisor |

#### Known Behavior: `S UFSD` fails with IEF612I while STC is running

**Observation:** While UFSD STC is active and subsystem "UFSD" is registered,
issuing `S UFSD` a second time fails with:
```
IEF612I PROCEDURE NOT FOUND
IEF452I UFSD JOB NOT RUN - JCL ERROR
IEE122I START COMMAND JCL ERROR
```
After `/P UFSD` (normal stop), `S UFSD` succeeds again.

**Explanation:** When "UFSD" is registered as a subsystem and MVS processes a
`S UFSD` command, MVS routes system-internal SSI calls (e.g. job-step
notifications) for that subsystem name through our UFSDSSIR. UFSDSSIR rejects
them (wrong eye-catcher or unsupported function code) and returns an error, which
MVS propagates as "PROCEDURE NOT FOUND". This is expected MVS behavior, not a bug.

**Workaround:** Do not start UFSD while it is already running. Stop first.

**Danger case — abend without cleanup:** If UFSD abends (S047, S0C4, etc.) the
normal shutdown path is NOT called. The SSCT entry remains registered. The next
`S UFSD` then fails the same way until IPL.

**Fix needed (AP-1c+):** Add ESTAE task-abend recovery in `ufsd.c` that calls
`ufsd_shutdown()` unconditionally on any abend. This ensures SSCT is always
deregistered before the address space terminates.

### AP-1d — DONE ✓

**Milestone achieved (MVS/CE):**

```
UFSD040I 2 disk(s) mounted
UFSD041I   UFSDISK0 DSN=IBMUSER.UFSD.UFSDISK0 (root)
UFSD041I   UFSDISK1 DSN=IBMUSER.UFSD.UFSDISK1
UFSTST01I Session opened, token=0x00010001
UFSTST02I Session closed
UFSD014I REQUESTS SERVED: 2
UFSD015I ERRORS:          0
UFSD050I ACTIVE SESSIONS: 0
```

Implemented in two steps:

**Step 1 — Session infrastructure:**
- `ufsd#ses.c`: session table (64 slots, STC heap), token scheme `((slot+1)<<16)|(serial&0xFFFF)`
- `ufsd#que.c`: UFSREQ_SESS_OPEN + UFSREQ_SESS_CLOSE dispatch
- `ufsd#cmd.c`: SESSIONS command (`/F UFSD,SESSIONS`)
- `client/ufsdtst.c`: UFSDTEST test client

**Step 2 — UFS disk integration:**
- `ufsd#ini.c`: TIOT scan for UFSDISK0-9, `osddcb`/`osdopen`/`__rdjfcb`, boot block
  validation, `UFSD_DISK_ROOT` flag on first disk
- `ufsd#ses.c`: `ufsd_sess_open` allocates `UFSD_UFS` handle (`cwd="/"`);
  `ufsd_sess_close` frees it
- JCL proc: `UFSDISK0`/`UFSDISK1` DD cards with `DISP=OLD`

**Open items:**
- ESTAE in `ufsd.c` to ensure SSCT is always deregistered on abend
- APF auth in clients (ufsdping, ufsdtst) is temporary PoC workaround

### AP-1e — DONE ✓

**Milestone achieved (MVS/CE):**

```
UFSD047I Global file table: 256 slots
UFSD040I 2 disk(s) mounted
UFSTST01I Session opened, token=0x00010001
UFSTST10I MKDIR /test: OK
UFSTST11I FOPEN /test/hello.txt (write): fd=0
UFSTST12I FWRITE 13 bytes: written=13
UFSTST13I FCLOSE write handle: OK
UFSTST14I FOPEN /test/hello.txt (read): fd=0
UFSTST15I FREAD 13 bytes: read=13
UFSTST16I FREAD data: Hello, World!
UFSTST17I FCLOSE read handle: OK
UFSTST18I REMOVE /test/hello.txt: OK
UFSTST19I RMDIR /test: OK
UFSTST02I Session closed
```

Note: The `!` character in "Hello, World!" renders as `Ü` on the MVS/CE console
due to CP273 (German EBCDIC) display — the on-disk data is correct.

New source modules implemented: `ufsd#blk.c`, `ufsd#sbl.c`, `ufsd#ino.c`,
`ufsd#dir.c`, `ufsd#gft.c`, `ufsd#fil.c`. All linked into the UFSD load module.

All seven AP-1e simplifications (indirect blocks, permissions, timestamps,
sb cache refill, inode cache, diropen/dirread/dirclose, fseek) remain deferred
as planned.

### AP-1f — Not started

---

## 2. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| IEFSSREQ mechanics on MVS 3.8j deviate from documentation | **High.** Blocks AP-1c and everything after. | Test early in AP-1c. Fallback: direct CSA communication without SSI (manual anchor lookup). |
| CSA memory insufficient (24-bit limit) | **Medium.** Pool sizes must be reduced. | Budget is ~101K. Monitoring via STATS. Pool sizes are configurable. |
| Loading SSI routine into CSA fails (addressability) | **Medium.** Blocks AP-1c. | Fallback: module directly in SYS1.LPALIB + IPL. Trivial on Hercules. |
| Performance unacceptable (IPC overhead) | **Low.** Functionally correct, just slow. | Phase 3 (multi-worker) addresses performance. Acceptable for PoC. |

## 3. Implementation Notes

### Reference Code

The HTTPD codebase (`github.com/mvslovers/httpd`) serves as primary reference:

- **CIB/QEDIT patterns:** `src/httpcmd.c` — command parsing, MODIFY handling
- **Thread management:** `src/httpd.c` — `cthread_create_ex()`, worker pool
- **UFS usage patterns:** `src/httpopen.c`, `src/httpfile.c` — fopen/fread/fclose
- **UFS initialization:** `src/httpconf.c` `process_httpd_ufs()` — `ufs_sys_new()`, `ufsnew()`
- **Startup skeleton:** `src/httpstrt.c` — `__start()` entry point, DD setup

### Build System

UFS370 and HTTPD both use `c2asm370` cross-compilation + `mvsasm`/`mvslink` via the mvsMF REST API. The UFSD STC will follow the same build pipeline. New source files integrate into the existing Makefile structure.

### Source File Layout (planned)

```
ufsd/
├── include/
│   └── ufsd.h          # All control block definitions
├── src/
│   ├── ufsd.c          # STC main program + main loop
│   ├── ufsd_csa.c      # CSA allocation and pool management
│   ├── ufsd_ssct.c     # SSCT/SSCVT registration
│   ├── ufsd_ssi.c      # SSI Thin Router (loaded into CSA)
│   ├── ufsd_queue.c    # Lock-free enqueue/dequeue
│   ├── ufsd_dispatch.c # Request validation + dispatch
│   ├── ufsd_session.c  # Session lifecycle
│   ├── ufsd_init.c     # UFS filesystem initialization
│   ├── ufsd_file.c     # File operation dispatch functions
│   ├── ufsd_gfile.c    # Global Open File Table
│   ├── ufsd_marshal.c  # Request/response serialization
│   ├── ufsd_mount.c    # MODIFY MOUNT/UNMOUNT handler
│   ├── ufsd_trace.c    # Trace ring buffer
│   └── ufsd_cmd.c      # MODIFY command parser
├── client/
│   ├── libufs.c        # Client stub library (~30 functions)
│   ├── ufsdping.c      # Ping/Pong test client
│   └── ufsdtest.c      # Integration test client
├── jcl/
│   └── UFSD.proc       # STC JCL procedure
├── Makefile
└── README.md
```
