# UFS370 STC/SUBSYS Design — Concept #8

Cross-Address-Space Filesystem Server for MVS 3.8j
Repository: github.com/mvslovers/ufsd
Revision 8 — Reflects completed Phase 4a (AP-4a)

---

## Changes from Concept #7

| # | Area | Change |
|---|------|--------|
| 1 | SSI timed WAIT | Unconditional WAIT replaced with `ecb_timed_wait` loop (5 s interval). Checks `UFSD_ANCHOR_ACTIVE` after each timeout; returns `UFSD_RC_CORRUPT` if server is dead. Prevents client hangs (AP-4a #2). |
| 2 | Superblock writeback | `ufsd_ufs_term` and `ufsd_disk_umount` call `ufsd_sb_write` for each RW disk before closing. Persists free block/inode caches (AP-4a #3). |
| 3 | Logging cleanup | All WTO messages follow `UFSDnnnS` pattern. `UFSD-DBG` messages removed. UFSDCLNP messages assigned 140-149 range. Message number conflicts resolved across modules (AP-4a #5). |
| 4 | Startup banner | `UFSD000I` before init, `UFSD001I` with version, disk count, CSA size, session/file slot summary (AP-4a #5). |
| 5 | Session cleanup | `SESSIONS PRUNE` command walks ASVT, detects terminated ASIDs, releases orphaned sessions with FDs and GFT entries (AP-4a #6). |
| 6 | Test consolidation | Removed `ufsdping` (superseded by `/F UFSD,STATS`) and `ufsdtst` (superseded by `LIBUFTST`). `LIBUFTST` retained as sole regression tool (AP-4a #7). |

<details><summary>Changes from Concept #6 (AP-3a + AP-3b)</summary>

| # | Area | Change |
|---|------|--------|
| 1 | Parmlib parser | `UFSDPRMx` configuration: ROOT, MOUNT statements with DSN/PATH/MODE/OWNER (AP-3a). |
| 2 | DYNALLOC mounts | All disk datasets opened via SVC 99 (`__svc99`/`__dsfree`), no DD cards in STC JCL (AP-3a). |
| 3 | Root disk RO | Root filesystem always read-only for clients after mount-point creation (AP-3a). |
| 4 | Mount traversal | Path walk crosses mount boundaries; `ufsd_find_disk` resolves longest-prefix mount (AP-3a). |
| 5 | Write check | `ufsd_check_write(disk, sess)` at 5 call sites: RO-mount and owner-restricted mount checks (AP-3a). |
| 6 | `ufs_setuser` | Client API to change session userid+group atomically for per-user permission checks (AP-3a). |
| 7 | SYNAD exit | Inline BR 14 stub prevents IEC020I on BDAM I/O errors (AP-3a). |
| 8 | Superblock validation | Boot block type/check verification on every disk open (AP-3a). |
| 9 | S99 error decoding | `s99_errmsg()` translates common S99ERROR codes to operator messages (AP-3a). |
| 10 | Permission bits | DIRREAD `attr[]` string built from actual inode mode bits, not hardcoded (AP-3b). |
| 11 | ASID in sessions | `ASCBASID` (offset 0x24) extracted from client ASCB in SSI router (AP-3b). |
| 12 | `ufs_sys_term` | Signature changed to `(UFSSYS **)`, frees the calloc'd UFSSYS block (AP-3b). |
| 13 | `mkdir_p` cleanup | Owner/group strings use `strcpy` instead of `memcpy` with trailing blanks (AP-3b). |
| 14 | Comment fixes | DIRREAD_RLEN corrected to 98 bytes; dispatch comment updated for `__xmpost` (AP-3b). |

</details>

---

## 1. Problem Statement

UFS370 is a Unix-like virtual filesystem for MVS 3.8j backed by BDAM datasets. All filesystem structures are bound to a single address space via `__wsaget()`. BDAM DCBs are also address-space-local. No external address space can access the filesystem.

UFSD solves this by running the filesystem as a Subsystem Started Task (STC) with a cross-address-space IPC protocol using CSA, IEFSSREQ (SVC 34), and lock-free queuing.

## 2. Architecture Overview

```
┌───────────────────────┐     ┌──────────────────────────┐
│ Client Address Space  │     │  UFSD STC Address Space  │
│                       │     │                          │
│  HTTPD / FTPD / App   │     │  Disk I/O Layer          │
│    │                  │     │    ├─ BDAM DCBs          │
│    ▼                  │     │    ├─ Superblock mgmt    │
│  libufs (client stub) │     │    │  ├─ V7 chain refill │
│    │                  │     │    │  ├─ Inode scan       │
│    ▼                  │     │    │  └─ Bitmap fallback  │
│  IEFSSREQ (SVC 34)   │     │    ├─ Inode I/O          │
│    │                  │     │    │  └─ Single indirect  │
│    ▼                  │     │    └─ Directory ops      │
│  SSI Router           │     │                          │
│  (ufsd#ssi)           │     │  Session Table (64 slots)│
│    │ R1=SSOB          │     │    ├─ fd_table[64]       │
│    │ __xmpost to wake │     │    ├─ CWD                │
│    │ timed WAIT (5s)  │     │    └─ owner/group        │
│    │ liveness check   │     │  Global File Table (256) │
│    ▼                  │     │  MODIFY commands         │
│  ┌─────── CSA ────────────────────────────────┐       │
│  │  UFSD_ANCHOR                               │       │
│  │  ├─ HEAD/TAIL (CS lock-free queue)         │       │
│  │  ├─ server_ecb / server_ascb               │       │
│  │  ├─ Request Pool (32 × ~296B, CS pop/push) │       │
│  │  ├─ 4K Buffer Pool (16 × 4K, CS pop/push) │       │
│  │  ├─ Trace Ring (256 × 16B)                │       │
│  │  └─ POST bundling (stat_posts_saved)       │       │
│  └────────────────────────────────────────────┘       │
│                       │     │  ESTAE recovery          │
│                       │     │  UFSDCLNP (emergency)    │
└───────────────────────┘     └──────────────────────────┘
```

## 3. Cross-AS Communication

**Constraint:** No Cross-Memory Services (PC/PT, ALESERV) on S/370. 

| Operation | Mechanism | Required State | Constraint |
|-----------|-----------|----------------|------------|
| POST cross-AS | `__xmpost(ascb, ecb_ptr, code)` via CVT0PT01 | Supervisor (key 0) | SVC 2 fails cross-AS from problem state |
| WAIT on ECB | `WAIT ECB=(ecb_addr)` via SVC 1 | Problem state | ECB must be key-8 storage (stack) |
| SSI routine entry | R1 = SSOB (MVS convention) | Supervisor (key 0) | Not a C plist — extract via inline asm |
| CSA pool ops | CS instruction (lock-free) | Supervisor (key 0) | Native S/370, no SVC overhead |

## 4. SSI Router (ufsd#ssi.c)

Runs in the **client address space**. Contains **zero business logic**.

**Phase 1 (enqueue + wake):**
1. Extract R1=SSOB via inline asm
2. Validate SSOB eye catcher
3. CS-pop request block from CSA free pool
4. Copy parameters + ACEE userid/group into request
5. FWRITE 4K: copy client buffer into CSA pool buffer
6. CS lock-free enqueue (queue_append → returns was_empty)
7. Conditional `__xmpost` (only if queue was previously empty)
8. Switch to problem state, WAIT on stack ECB (key-8)

**Phase 2 (result + cleanup):**
1. Switch to supervisor state (key-0)
2. FREAD 4K: copy CSA buffer to client destination, free buffer
3. Copy result (rc, data) back to SSOB extension
4. CS-push request block back to free pool
5. Switch to problem state

## 5. CSA Memory Model

| Pool | Count | Unit Size | Total | Allocation |
|------|-------|-----------|-------|------------|
| Request | 32 | ~296B | ~9K | CS lock-free stack |
| Buffer | 16 | 4K | 64K | CS lock-free stack |
| Trace | 256 | 16B | 4K | Ring buffer |
| Anchor | 1 | ~512B | ~1K | Fixed |
| **Total** | | | **~78K** | |

All CSA storage is key-0, allocated via GETMAIN SP=241.

## 6. Session and File Descriptor Model

Two-level model, no shared position (no fork on MVS):

```
Client fd → Session fd_table[64] → Global Open File (position, refcount) → Inode
```

- **Session:** 64 slots, token = `(slot+1)<<16 | serial`. Captures ACEE owner/group at open.
- **Global File Table:** 256 entries, always refcount=1 in Phase 1.
- **File Descriptors:** 64 per session, map to GFT entries.

Session lifecycle: OPEN → use → CLOSE. ABEND cleanup via ASID scan.

## 7. Disk I/O and On-Disk Format

UFSD reimplements block I/O directly (does not link UFS370 library). The on-disk format is fully specified in `doc/ufsdisk-spec.md`.

Key characteristics:
- Block sizes: 512–8192 (default 4096)
- 128-byte inodes with UFSTIMEV (V1/V2 dual-format timestamps)
- 64-byte directory entries (59-char filenames)
- V7 free block chain + 64-entry free inode cache
- EBCDIC strings, big-endian byte order

### 7.1 Block Addressing

| Index | Purpose | Max Size (4K blocks) |
|-------|---------|---------------------|
| addr[0..15] | Direct blocks | 64 KB |
| addr[16] | Single indirect | 4.06 MB |
| addr[17] | Double indirect | Not implemented |
| addr[18] | Triple indirect | Not implemented |

### 7.2 Free Block Management

Three-tier refill when superblock cache (51 entries) is exhausted:

1. **V7 chain refill:** Read the chain block, validate count + block numbers, refill cache. O(1).
2. **Bitmap scan fallback:** Scan all inodes, build used-block bitmap, collect free blocks. O(n). Used when chain is broken.
3. **Manual REBUILD:** `/F UFSD,REBUILD` forces both block and inode cache rebuild.

### 7.3 Free Inode Management

Cache of 64 entries. When exhausted:
- **Inode scan refill:** Scan inode blocks for `mode==0`, fill cache. O(n).
- WTO `UFSD076I` confirms refill.

## 8. Configuration and Mount Model

### 8.1 Parmlib Configuration

UFSD is configured via a `SYS1.PARMLIB(UFSDPRMx)` member, analogous to
`BPXPRMxx` on z/OS. No DD cards for disks in the STC JCL — all datasets
are opened dynamically via SVC 99 (DYNALLOC) based on the Parmlib config.

```
/* SYS1.PARMLIB(UFSDPRM0) */

/* Root filesystem — auto-created by UFSD if missing */
ROOT     DSN(SYS1.UFSD.ROOT) SIZE(1M) BLKSIZE(4096)

/* Web server content (shipped with HTTPD, read-only) */
MOUNT    DSN(HTTPD.WEBROOT)         PATH(/www)          MODE(RO)

/* Custom sites (read-write, owner-restricted) */
MOUNT    DSN(IBMUSER.SITE.SHOP)     PATH(/sites/shop)   MODE(RW) OWNER(IBMUSER)

/* User home directories */
MOUNT    DSN(IBMUSER.UFSHOME)       PATH(/u/IBMUSER)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(HERC02.UFSHOME)        PATH(/u/HERC02)     MODE(RW) OWNER(HERC02)

/* Anonymous FTP area */
MOUNT    DSN(FTP.PUBLIC)            PATH(/ftp/pub)       MODE(RW) OWNER(FTPANON)
```

### 8.2 Root Disk

On first startup, if the ROOT dataset does not exist, UFSD allocates it
(DYNALLOC), formats it (`ufsd_disk_format`), and creates `/etc/`. The root
disk is always mounted read-only for all clients. It provides the top-level
namespace (`/www`, `/u`, `/sites`, `/ftp` are mount points on the root disk).

### 8.3 Mount Namespace

Each BDAM dataset is mounted on a path in a unified directory tree:

```
/                        SYS1.UFSD.ROOT        RO   (auto-created)
/www                     HTTPD.WEBROOT          RO   (default website)
/sites/shop              IBMUSER.SITE.SHOP      RW   OWNER(IBMUSER)
/u/IBMUSER               IBMUSER.UFSHOME        RW   OWNER(IBMUSER)
/ftp/pub                 FTP.PUBLIC             RW   OWNER(FTPANON)
```

Mount points must exist as directories on the parent filesystem before
the child filesystem is mounted. UFSD creates them on the root disk
automatically when processing MOUNT statements.

### 8.4 Access Control

Three levels, applied in order:

**Level 1: Mount mode (RO/RW).** Per-disk flag from Parmlib or MODIFY MOUNT.
Read-only mounts reject all write operations (FOPEN write, FWRITE, MKDIR,
RMDIR, REMOVE) with `UFSD_RC_ROFS`.

**Level 2: Owner check.** If the mount has an OWNER, only sessions whose
ACEE userid matches the OWNER (or a future admin group) may write. Other
sessions get `UFSD_RC_EACCES`. Mounts without OWNER allow any authenticated
user to write (if MODE=RW).

**Level 3: RACF/RAKF integration.** Deferred to a future phase. Would use
RACHECK per operation. Not needed for current use cases.

The check is applied in `do_fopen` (write mode), `do_mkdir`, `do_rmdir`,
`do_remove`, and `do_fwrite` — five call sites, one helper function:

```c
int ufsd_check_write(UFSD_DISK *disk, UFSD_SESSION *sess);
/* Returns UFSD_RC_OK, UFSD_RC_ROFS, or UFSD_RC_EACCES */
```

### 8.5 Client Responsibility

UFSD provides the namespace and access control. Application-level routing
is the client's job:

- **HTTPD:** Configures `DOCROOT=/www`, virtual hosts via `VHOST ... DOCROOT=/sites/xxx`,
  user directories via `USERDIR=/u/*/public_html`. Pure URL-to-path mapping.
- **FTPD:** Sets CWD to user home `/u/USERNAME` on login. Anonymous users
  get CWD `/ftp/pub` with chroot (FTPD prevents `..` above chroot).
- **TSO/Batch:** Opens UFSD session, navigates freely within permitted mounts.

### 8.6 Dynamic Mounts

Mounts can be added at runtime without restart:

```
/F UFSD,MOUNT DSN=HERC02.SITE.WIKI,PATH=/sites/wiki,MODE=RW,OWNER=HERC02
/F UFSD,UNMOUNT PATH=/sites/wiki
```

Dynamic mounts are not persisted — they are lost on UFSD restart. To make
them permanent, add them to `UFSDPRMx`.

## 9. Operator Commands

Via `/F UFSD,<command>` using CIB/QEDIT:

| Command | Description |
|---------|-------------|
| `/F UFSD,MOUNT DSN=x,PATH=y[,MODE=RO\|RW][,OWNER=z]` | Mount BDAM dataset on path |
| `/F UFSD,UNMOUNT PATH=y` | Unmount path |
| `/F UFSD,STATS` | Statistics (requests, errors, posts_saved, free counts) |
| `/F UFSD,SESSIONS` | List active sessions |
| `/F UFSD,TRACE ON\|OFF\|DUMP` | Control trace ring buffer |
| `/F UFSD,REBUILD` | Force free block + inode cache rebuild |
| `/F UFSD,SHUTDOWN` | Orderly shutdown |
| `/P UFSD` | Standard MVS STOP |

## 10. Client Library (libufs)

`client/libufs.c` — ~30 functions providing POSIX-like API over UFSD IPC:

- **Session:** `ufs_init`, `ufs_term`
- **File I/O:** `ufs_fopen`, `ufs_fclose`, `ufs_fread`, `ufs_fwrite`, `ufs_fgetc`, `ufs_fputc`, `ufs_fputs`, `ufs_fgets`
- **Directory:** `ufs_mkdir`, `ufs_rmdir`, `ufs_chdir`, `ufs_remove`, `ufs_getcwd`, `ufs_opendir`, `ufs_readdir`, `ufs_closedir`
- **Optimizations:** 252-byte read-ahead buffer, 4K write-behind buffer, 4K CSA buffer pool path for large reads/writes

## 11. Source Modules

| Module | File | Lines | Description |
|--------|------|-------|-------------|
| UFSD | `src/ufsd.c` | 320 | STC main, main loop, ESTAE, shutdown |
| UFSD#CMD | `src/ufsd#cmd.c` | 316 | MODIFY command parser |
| UFSD#CSA | `src/ufsd#csa.c` | 220 | CSA allocation, request pool |
| UFSD#BUF | `src/ufsd#buf.c` | 78 | 4K buffer pool (CS lock-free) |
| UFSD#SCT | `src/ufsd#sct.c` | 159 | SSCT/SSVT registration |
| UFSD#SSI | `src/ufsd#ssi.c` | 367 | SSI thin router |
| UFSD#QUE | `src/ufsd#que.c` | 262 | Lock-free queue, dispatch routing |
| UFSD#SES | `src/ufsd#ses.c` | 315 | Session management |
| UFSD#INI | `src/ufsd#ini.c` | 393 | Disk init (DYNALLOC, BDAM open, mount) |
| UFSD#BLK | `src/ufsd#blk.c` | 59 | BDAM block read/write |
| UFSD#SBL | `src/ufsd#sbl.c` | 464 | Superblock, alloc, chain/inode/bitmap refill |
| UFSD#INO | `src/ufsd#ino.c` | 85 | Inode I/O |
| UFSD#DIR | `src/ufsd#dir.c` | 326 | Directory lookup/add/remove |
| UFSD#FIL | `src/ufsd#fil.c` | 1241 | File operation dispatch |
| UFSD#GFT | `src/ufsd#gft.c` | 119 | Global File Table |
| UFSD#TRC | `src/ufsd#trc.c` | 109 | Trace ring buffer |
| UFSDCLNP | `src/ufsdclnp.c` | 132 | Emergency cleanup (no-IPL recovery) |
| LIBUFS | `client/libufs.c` | 864 | Client stub library |
| **Total** | | **5829** | Server + client (excl. test programs) |

Test programs: `client/ufsdping.c` (80), `client/ufsdtst.c` (330), `client/libufstst.c` (228).

## 12. Known Limitations

### 12.1 Active — Must Fix

| # | Issue | Impact | Effort |
|---|-------|--------|--------|
| 1 | **Freeblock cache overflow** on mass delete | `blk_free_all` on a file with indirect block (up to 1040 blocks) leaks blocks beyond cache[51]. Lost until REBUILD/remount. | Medium — write chain-block on overflow (inverse of chain_refill) |
| 2 | **Freeinode cache overflow** | Same for inode cache[64], but less likely (rarely 64+ deletes in one operation). | Low — same pattern as #1 |

### 12.2 Accepted — Current Design

| # | Limitation | Notes |
|---|-----------|-------|
| 3 | Single-threaded dispatch | Slow BDAM I/O blocks all clients. Acceptable for single-user/low-load. Phase 4. |
| 4 | No double/triple indirect blocks | Max file 4.06 MB. Sufficient for web content. |
| 5 | No RACF per-operation checking | Access control via mount-mode + owner check. Full RACHECK deferred. |
| 6 | No NLST in FTPD | Not in reference implementation either. |
| 7 | DIRREAD: direct blocks only | Directory with >1024 entries (4K blocks) would need indirect support. Unrealistic for MVS. |
| 8 | No fseek | Sequential access only. Could be added to libufs if needed. |

## 13. Companion Projects

| Project | Language | Purpose |
|---------|----------|---------|
| `mvslovers/ufsd` | C / HLASM | Server + client (this project) |
| `mvslovers/ufsd-utils` | Go | Host-side CLI: create, info, ls, cp, cat, mkdir |
| `mvslovers/ufs370` | C | Original UFS370 library (maintenance mode) |
| `mvslovers/ufs370-tools` | C | MVS-side FORMAT tool (may merge into ufsd) |

## 14. Phase Plan

| Phase | Status | Scope | Key Deliverables |
|-------|--------|-------|------------------|
| **1** | ✅ Done | STC + CSA + File Ops | STC skeleton, SSCT, CSA queue, SSI router, sessions, fd_table, all file ops (FOPEN–REMOVE), trace, MODIFY, ESTAE |
| **2** | ✅ Done | Client Stubs + Integration | libufs (~30 functions), HTTPD/FTPD integration, MOUNT/UNMOUNT, diropen/dirread/dirclose, GETCWD |
| **2a** | ✅ Done | Post-PoC Hardening | FWRITE 4K, timestamps, POST bundling, CS buf pool, write-behind, chain/inode refill, path validation, UFSDCLNP |
| **3** | ✅ Done | Config + Mount Model | Parmlib, DYNALLOC, root-disk RO, mount traversal, check_write, ufs_setuser, SYNAD, superblock validation, S99 decode |
| **4a** | ✅ Done | Beta Readiness | SSI timed WAIT, SB writeback, logging cleanup, session PRUNE, ufs_stat, RC fixes, ufsdrc.h, README |
| **4b** | 🔲 Open | Beta Remaining | /F UFSD,CREATE, nested mount fix, ESTAE cleanup, UFSDCLNP separate proc |
| **5** | 🔲 Future | Multi-Worker | Pre-ATTACHed worker pool, inode/dir locking, SSI to LPA |
| **6** | 🔲 Future | Extensions | VFS abstraction, double indirect, full RACF, pager/cache, ufsd-utils fsck |

## 15. Test Infrastructure

- **FTP Test Suite v4:** 15 test cases, 46 assertions. Covers timestamps, data integrity, chain-refill, edge sizes, mass upload, path validation, error handling, performance. (`ufsd-ftp-test.sh`)
- **HTTP Test:** TTFB + total time comparison vs. reference.
- **LIBUFTST:** libufs API regression test (sole remaining test tool, replaces ufsdtst/ufsdping).
- **ufsd-utils:** Host-side image inspection and verification.
