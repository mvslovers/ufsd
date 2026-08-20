# Recovery and Troubleshooting

This document covers UFSD recovery procedures, the emergency cleanup utility, and known issues.

## Normal Shutdown

Stop the daemon with `/P UFSD` or `/F UFSD,SHUTDOWN`. UFSD performs an orderly shutdown:

1. Nulls the SSVT function entry and clears `UFSD_ANCHOR_ACTIVE` (no new SSI dispatches; parked clients bail on their next timeout)
2. Writes superblocks back to disk for all RW-mounted filesystems and closes all BDAM datasets (DYNFREE)
3. Drains in-flight SSI clients — waits for `anchor->inflight` to reach zero (~10 s ceiling); on timeout it retains the CSA and issues `UFSD098W`
4. Deregisters the SSCT (freeing the SSVT) and unloads the SSI router (UFSDSSIR) from CSA
5. Frees all CSA pools (requests, buffers, trace) and the anchor

Console output:

```
UFSD098I UFSD SHUTTING DOWN
UFSD131I SUPERBLOCK WRITTEN FOR DSN=IBMUSER.UFSHOME
UFSD131I SUPERBLOCK WRITTEN FOR DSN=UFSD.SCRATCH
UFSD095I SSCT DEREGISTERED
UFSD036I SSI ROUTER UNLOADED
UFSD096I CSA FREED
UFSD099I UFSD SHUTDOWN COMPLETE
```

(One `UFSD131I` per RW-mounted filesystem; RO mounts are not written back. If a
client is still in flight when the stop is issued, a short drain delay precedes
`UFSD095I`; if the drain does not reach zero within its ceiling the daemon
issues `UFSD098W` and retains the CSA instead — the next `/S UFSD` reclaims it
automatically.)

## Recovery after Abend

If UFSD abends (S0C4, S222 from `/C`, etc.), the ESTAE handler runs in emergency mode (`UFSD_SHUT_ABEND`): it nulls the SSVT function entry and clears `UFSD_ANCHOR_ACTIVE` — stopping new SSI dispatches and letting parked clients bail — then percolates for the MVS dump. It deliberately frees **nothing**: under RTM there is no reliable way to tell whether another address space is still executing inside the router module or the CSA pools, and freeing them then is exactly the S0C4 being recovered from. SSCT, SSI router, and CSA pools are therefore left allocated; the final console message states this (`UFSD097W … CSA RETAINED, RECLAIMED AT NEXT START`). This is by design, not a defect (see [CSA Retained after /C or Abend](#csa-retained-after-c-cancel-or-abend-by-design)).

### Recovery Procedure

Restart UFSD:

```
/S UFSD
```

Startup detects the orphaned predecessor itself (`ufsd_reclaim()`, shared with UFSDCLNP) and reclaims it before registering — quiesce, drain, deregister, free — so no separate cleanup step is needed.

No prerequisite beyond the STC procedure itself. The start goes through JES2 and `SYS2.PROCLIB(UFSD)` whether or not an orphan is present, because the subsystem is named `UFS1` and the procedure `UFSD` — see [Why the subsystem is not called UFSD](#why-the-subsystem-is-not-called-ufsd).

Console output of a start over an orphan (MVS-verified 2026-08-10, captured
before the #51 banner change — `UFSD000I` now also carries the build commit
and is followed by `UFSD005I`; the reclaim sequence itself is unchanged):

```
UFSD000I UFSD 1.1.0 STARTING
UFSD144I RECLAIM: SSVT FUNCTION POINTER CLEARED
UFSD145I RECLAIM: SSCT DEREGISTERED
UFSD146I RECLAIM: SSI ROUTER MODULE FREED
UFSD147I RECLAIM: CSA POOLS FREED
UFSD148I RECLAIM: ANCHOR FREED
UFSD004I ORPHANED PREDECESSOR RECLAIMED -- CSA RECOVERED
UFSD030I CSA ALLOCATED: ANCHOR=...
```

(If clients were parked in the router when UFSD died, a short drain delay
precedes `UFSD145I` while they bail; a count still standing after the
ceiling yields `UFSD152W … ASSUMING LEAKED, FREEING` and reclaim proceeds.)

This covers the FORCE case too. A UFSD killed before its ESTAE could run leaves `UFSD_ANCHOR_ACTIVE` set, which used to make startup refuse; since #53 the reclaim looks the anchor's `server_ascb` up in the ASVT, finds no such address space, and reclaims the orphan anyway. `/S UFSD` is the answer to every abnormal end of a UFSD instance.

What startup still refuses is a predecessor that is genuinely **running** (`UFSD155W` names its ASCB, then `UFSD002E UFSD ALREADY REGISTERED AND ACTIVE`) — as it must; two servers cannot own the same CSA. The one remaining case for the standalone utility is an anchor flagged ACTIVE whose ASCB cannot be checked at all (`UFSD156W`), which needs the `force` only UFSDCLNP passes:

```
/S UFSDCLNP
/S UFSD
```

### UFSDCLNP Details

UFSDCLNP is a standalone program wrapping the same shared reclaim routine (`ufsd_reclaim()`, `src/ufsd#rcl.c`) that UFSD startup runs. It locates the UFSD anchor in CSA via the SSCT chain, then tears down all resources:

1. Nulls the SSVT function pointer (no new SSI dispatches) and clears `UFSD_ANCHOR_ACTIVE` (a client parked in the router bails on its next timeout)
2. **Drains `anchor->inflight` to zero** — keeping the eye catcher and the router module present — so a parked client bails `RC_CORRUPT` and leaves the module *before* it is freed (best-effort, ~12 s ceiling; see below)
3. Deregisters and frees the SSCT
4. Unloads the SSI router module from CSA
5. Frees all CSA pools (trace ring, buffer pool, request pool)
6. Invalidates the eye catcher and frees the anchor

The drain (step 2) mirrors the clean-`/P` path and closes the window where freeing the router module out from under a parked client would S0C4 that client's address space (issue #39). It is **best-effort**: a count still standing after the ceiling is almost certainly leaked — a client that faulted or whose address space was cancelled while in-flight never runs its decrement — so UFSDCLNP warns (`UFSD152W … ASSUMING LEAKED, FREEING`) and frees anyway. Stranding CSA is the one failure UFSDCLNP exists to prevent, and it has no fallback but an IPL.

UFSDCLNP is safe to run when UFSD is not registered — it reports "nothing to do" and exits RC=0.

### The Liveness Guard (#53)

Steps 1–6 are exactly what must never happen to a *running* UFSD: clearing `UFSD_ANCHOR_ACTIVE` and freeing the CSA pulls the storage out from under the live STC. Before #53, UFSDCLNP had no way to tell the two apart and did it anyway.

It now refuses. Before touching anything, the reclaim takes the STC's ASCB from the anchor (`server_ascb`, recorded at startup for cross-AS POST) and looks it up in the ASVT. Only an ASCB that belongs to no currently assigned address space is treated as an orphan's:

| Anchor state | `/S UFSD` (reclaim) | `/S UFSDCLNP` (`force`) |
|---|---|---|
| ACTIVE clear — a shutdown path ran | reclaims | reclaims |
| ACTIVE, ASCB assigned in the ASVT | refuses (`UFSD155W` + `UFSD002E`) | refuses (`UFSD155W` + `UFSD157W`, RC 8) |
| ACTIVE, ASCB in no ASVT entry — killed before the ESTAE ran | reclaims | reclaims |
| ACTIVE, ASCB is the reclaiming address space's own | reclaims | reclaims |
| ACTIVE, no ASCB to look up | refuses (`UFSD156W` + `UFSD002E`) | reclaims |

So `force` no longer means "unconditionally": it covers only the last row. A running server is off limits to UFSDCLNP as well, and there is no override — stop it first.

Row four is what makes the FORCE recovery work at all, and it is not a corner case. An ASCB block goes back to SQA when its address space ends, and the address space created next — usually the restart itself — can be handed the same block. Measured on mvsdev: UFSD ran in ASCB `00FD40D0`, was killed, and the restart came up in `00FD40D0` again. Without excluding the caller's own ASCB, that restart would find the predecessor's address in the ASVT, assigned to itself, and refuse — and so would UFSDCLNP, leaving only an IPL. The exclusion cannot hide a live server: two address spaces alive at the same time never share an ASCB address.

Row three is rarer than it looks, because MVS makes it hard to produce: `FORCE` is rejected for a cancelable address space (`IEE838I … CANCELABLE - ISSUE CANCEL BEFORE FORCE`), and a `CANCEL` reaches UFSD's ESTAE, which clears the flag on its way out. The state therefore arises from a UFSD hung badly enough that the CANCEL never completes, or from the `UFSD097E` path where shutdown cannot enter supervisor state.

The comparison is on the ASCB address alone; nothing is read out of the ASCB itself, because SQA storage of an ended address space can be reused and a jobname read out of a reused block would be another address space's. That leaves one residual error, and it is the harmless one: if the ASCB address has been handed to some *third* address space, a genuinely dead UFSD looks alive and cleanup is refused. Check with `D A,L` — if no UFSD address space is listed, the SSCT is an orphan the guard cannot recognize, and an IPL is the way out.

**A hung but living UFSD is not a UFSDCLNP case.** The guard sees an assigned ASCB and refuses regardless of whether the server still answers. Take the address space down first (`/P UFSD`, then `/C UFSD`, then FORCE), and only then reclaim — after a FORCE, `/S UFSD` already does it.

### Installation

Copy the UFSDCLNP STC procedure from `samplib/ufsdclnp` to `SYS2.PROCLIB(UFSDCLNP)`. Nothing has to be installed in `SYS1.PROCLIB`.

## Why the Subsystem is not called UFSD

The MVS subsystem is named **`UFS1`** (`UFSD_SSNAME`, `include/ufsd.h`); only the started task keeps the name `UFSD`. `F UFSD,…` and `P UFSD` address the jobname and are unaffected.

The two must differ. While a subsystem of the same name is registered, MVS converts `S <name>` onto the **master-subsystem** start path, and the master's `IEFPDSI` knows only `SYS1.PROCLIB` — so with a subsystem named `UFSD`, every start issued over a registered SSCT (the reclaim start after an abend, or a second start while the server runs) either failed with `IEF612I PROCEDURE NOT FOUND` before any UFSD code ran, or had to be served by a companion member in `SYS1.PROCLIB`. That companion then shadowed `SYS2.PROCLIB(UFSD)` for *all* starts, because JES2's `PROC00` concatenation searches `SYS1.PROCLIB` first. This is the anti-pattern IBM later codified as "do not assign a subsystem the same name as a started procedure"; the non-JES products avoid it by naming (DB2 `DSN1`, MQ `MQ01`).

Note that the name is compiled into every client through the statically linked libufs, so server and consumers (HTTPD, FTPD, mvsMF, HTTPLUA, HTTPREXX) must be deployed together.

### Upgrading from UFSD 1.0.0

The rename is not backward compatible in either direction — a 1.0.0 client cannot find `UFS1`, and a 1.1.0 server does not see an SSCT named `UFSD`. On a system that ran UFSD 1.0.0:

1. Stop the server cleanly (`/P UFSD`) — the stop is not optional, since UFSDCLNP refuses while the address space is still there (see [The Liveness Guard](#the-liveness-guard-53)). If it is already gone but left an orphan behind, run `/S UFSDCLNP` **with the old load modules still installed** — its reclaim looks for the old name, and once the new modules are in place nothing will find that SSCT again. An IPL clears it just as well.
2. Delete `SYS1.PROCLIB(UFSD)` if the MSTR companion was installed. It is no longer needed, and left in place it keeps shadowing `SYS2.PROCLIB(UFSD)` — the server then starts from the wrong member, silently, with `SYSPRINT`/`SYSTERM` on `DUMMY` and dumps going somewhere other than the spool. Verify with `/S UFSD` that no `IEF196I` JCL echo appears.
3. Deploy the new UFSD **and** every rebuilt consumer, then start.

A leftover `UFSD.SYSUDUMP` data set from the companion can be deleted; the STC procedure uses `SYSUDUMP SYSOUT=*` again.

## Known Issues

### CSA Retained after /C CANCEL or Abend (by design)

After `/C UFSD` or any abend, the ESTAE handler runs in `UFSD_SHUT_ABEND` mode: it nulls the SSVT function entry and clears `UFSD_ANCHOR_ACTIVE` (so `iefssreq` stops routing new clients into UFSDSSIR and any parked client bails on its next router timeout), then percolates. It **frees nothing** — under RTM a foreign PSW may still be executing inside the router module or touching the CSA pools, and freeing them then is the very S0C4 this behaviour prevents.

The **designed** recovery after `/C` or an abend is simply the next `/S UFSD`: startup runs the shared reclaim routine (`ufsd_reclaim()`, `src/ufsd#rcl.c`) before registering. It nulls the SSVT entry (idempotent), clears `UFSD_ANCHOR_ACTIVE`, then **drains `anchor->inflight` to zero before it frees anything** — keeping the eye catcher and the router module present throughout. A client still parked in the router revalidates the (still-valid) eye catcher on its next timeout, sees the cleared flag, and bails `RC_CORRUPT`, decrementing the counter as it leaves. Only once the count reaches zero does the reclaim deregister the SSCT and free the router module, pools, and anchor (invalidating the eye catcher just before the anchor FREEMAIN). This closes the window — present before #39 — where freeing the router out from under a parked client faulted that client's address space (`S0C4`; contained by a client-side ESTAE such as HTTPD's, fatal to a bare libufs batch client). The same routine backs the standalone `/S UFSDCLNP`.

The drain is **best-effort**, ceiling ~12 s (two `UFSD_WAIT_INTERVAL`s, so a live parked client gets two wake-and-bail chances). A count still standing after that is almost certainly leaked — a client that faulted or was cancelled while in-flight never runs its decrement — so the reclaim warns (`UFSD152W`) and frees anyway rather than strand the CSA. So a genuinely stuck worker does not hang the cleanup indefinitely; worst case the reclaim waits ~12 s, then frees.

A clean `/P UFSD` (or `/F UFSD,SHUTDOWN`) runs in `UFSD_SHUT_NORMAL` mode instead: it nulls the SSVT entry, clears `UFSD_ANCHOR_ACTIVE`, then **drains** `anchor->inflight` to zero and frees the CSA itself — nothing left behind after a normal stop. If the drain does not reach zero within its ceiling (~10 s), UFSD issues `UFSD098W` and retains the CSA rather than freeing storage a client may still be executing; the next `/S UFSD` reclaims it.

**TSK-71**: the retained CSA after `/C` or an abend is intended; since #49 the next `/S UFSD` reclaims it automatically, and the abend path's final WTO states the CSA state (`UFSD097W`) instead of a misleading `SHUTDOWN COMPLETE`.

### No Per-File Permission System

Access control is mount-level only (RO/RW mode + OWNER restriction). The `mode` bits stored in inodes are informational only and not enforced by UFSD. Full RACF/RAKF integration with per-operation RACHECK is planned for a future release.

### Single-Threaded Dispatch

The UFSD dispatch loop is single-threaded. Slow BDAM I/O on one request blocks all other clients. This is acceptable for single-user or low-load scenarios. Multi-worker support (pre-ATTACHed worker pool) is planned for a future release.

### File Size Limit

Maximum file size is 4.06 MB (16 direct blocks + 1024 single indirect blocks at 4K block size). Double and triple indirect blocks are not implemented. This is sufficient for web content (HTML, CSS, JS).

### No File Locking

Concurrent writes to the same file from different sessions are not serialized. Applications must coordinate access externally.

## WTO Message Reference

All UFSD messages follow the `UFSDnnnX` pattern, where `nnn` is the message number and `X` is the severity (I=informational, W=warning, E=error).

### Startup and Shutdown

| Message | Severity | Description |
|---------|----------|-------------|
| UFSD000I | I | UFSD starting — version + build commit (`-DIRTY` when built from a modified tree) |
| UFSD001I | I | Ready — version + CSA/session/file summary |
| UFSD002E | E | Predecessor SSCT belongs to a running UFSD (`UFSD155W`) or to an anchor whose ASCB cannot be checked (`UFSD156W`) — refusing to start |
| UFSD003E | E | Predecessor reclaim failed (supervisor state) — startup fails |
| UFSD004I | I | Orphaned predecessor reclaimed at startup — CSA recovered |
| UFSD005I | I | libc370 version + commit this module was linked against |
| UFSD006W | W | Built from a modified working tree — issued only by a `-DIRTY` build |
| UFSD007I | I | Which route authorized the STC: `AUTHORIZED BY LIBRARY` (already authorized at entry, from the APF list) or `AUTHORIZED BY SVC` (self-authorized via SVC 244). The module key that follows is inferred from the route, not measured — an authorized job step has its module fetched key 0, so module storage is read-only to the STC (see issue #64) |
| UFSD030I | I | CSA anchor allocated |
| UFSD031I–033I | I | Pool sizes (request pool / buffer pool / trace buffer) |
| UFSD034I | I | SSCT registered |
| UFSD035I | I | SSI router (UFSDSSIR) loaded into CSA |
| UFSD045I | I | Session table allocated |
| UFSD047I | I | Global file table allocated |
| UFSD040I | I | Mount summary (n filesystems mounted) |
| UFSD060I | I | Mounted DSN on path — mode `RW`/`RO`, or `ROOT` for `/` |
| UFSD090E | E | Cannot initialize console interface (startup fails) |
| UFSD091E | E | APF setup failed — STEPLIB not APF-authorized (startup fails) |
| UFSD092E | E | Cannot allocate CSA anchor (startup fails) |
| UFSD098I | I | Shutdown starting (normal shutdown only; the abend path announces itself with UFSD098E instead) |
| UFSD130W | W | Superblock writeback failed for a disk at shutdown |
| UFSD131I | I | Superblock written for DSN (one per RW filesystem) |
| UFSD095I | I | SSCT deregistered |
| UFSD036I | I | SSI router unloaded from CSA |
| UFSD046I | I | Session table freed |
| UFSD048I | I | Global file table freed |
| UFSD096I | I | CSA freed |
| UFSD097E | E | Cannot enter supervisor state at shutdown — CSA retained, run UFSDCLNP (ACTIVE flag stays set, so startup will refuse the orphan) |
| UFSD097W | W | Abend shutdown — CSA retained, reclaimed at next start (final message of the abend path) |
| UFSD098E | E | Abend intercepted — emergency shutdown |
| UFSD098W | W | Client(s) still in flight at shutdown — CSA retained, reclaimed at next start |
| UFSD099I | I | Shutdown complete (normal shutdown only; not issued on the abend path) |

### Configuration and Mounts (060–079, 120–129)

| Message | Severity | Description |
|---------|----------|-------------|
| UFSD061E | E | Parmlib not found |
| UFSD076I | I | Inode cache refill complete |
| UFSD122W | W | Cannot create mount point |
| UFSD123W | W | Cannot mount dataset |
| UFSD125W | W | The disk was formatted for one userid and mounted with `OWNER()` naming another. Both are kept: `OWNER()` decides who may write, the root inode owner is metadata that no permission check reads. Correct whichever is wrong — the parmlib statement, or the disk (reformat) |
| UFSD126W | W | The mount point directory does not exist on the parent filesystem. The filesystem is mounted and reachable by path; only a listing of the parent directory keeps showing the mount point's own metadata instead of the mounted root's |

### Parmlib Parser (100–105)

Issued by the `DD:UFSDPRM` parser at startup. A rejected statement is
skipped; the rest of the member is still processed.

| Message | Severity | Description |
|---------|----------|-------------|
| UFSD100W | W | Cannot open DD:UFSDPRM — using defaults |
| UFSD101W | W | Too many MOUNT statements — the rest are ignored |
| UFSD102W | W | MOUNT without DSN() — statement skipped |
| UFSD103W | W | MOUNT without PATH() — statement skipped |
| UFSD104W | W | Unrecognized statement (first 40 characters echoed) |
| UFSD105W | W | ROOT statement missing — startup fails |

### Reclaim and UFSDCLNP (140–149, 152–157)

Messages 143–148 and 152–156 come from the shared reclaim routine
(`RECLAIM:` prefix) and appear both during UFSD startup (over an orphaned
predecessor) and under standalone UFSDCLNP.

| Message | Severity | Description |
|---------|----------|-------------|
| UFSD140I | I | UFSDCLNP starting |
| UFSD141E | E | APF setup failed (STEPLIB not APF-authorized?) |
| UFSD142I | I | Subsystem UFS1 not registered — nothing to do (RC 0) |
| UFSD143E | E | Reclaim: cannot enter supervisor state |
| UFSD144I | I | Reclaim: SSVT function pointer cleared |
| UFSD145I | I | Reclaim: SSCT deregistered |
| UFSD146I | I | Reclaim: SSI router module freed |
| UFSD147I | I | Reclaim: CSA pools freed |
| UFSD148I | I | Reclaim: anchor freed |
| UFSD149I | I | UFSDCLNP complete |
| UFSD152W | W | Reclaim: client(s) still in flight after the drain ceiling — assumed leaked, CSA freed anyway |
| UFSD153W | W | Reclaim: anchor eye-catcher mismatch — anchor not freed |
| UFSD154I | I | Reclaim: no anchor (ssctsuse=NULL) — nothing to free |
| UFSD155W | W | Reclaim: the server's ASCB is an assigned address space — UFSD is running, nothing reclaimed (verify with `D A,L`) |
| UFSD156W | W | Reclaim: anchor flagged ACTIVE but its ASCB cannot be checked — nothing reclaimed (UFSDCLNP passes `force` and reclaims) |
| UFSD157W | W | UFSDCLNP: cleanup suppressed, stop UFSD first (RC 8; follows UFSD155W) |

> **Note:** The message numbers listed here reflect the current codebase. Exact numbers may differ slightly — consult the source for authoritative message IDs.
