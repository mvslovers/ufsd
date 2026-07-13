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
UFSD090I UFSD shutting down
UFSD091I Superblock written for DSN=IBMUSER.UFSHOME (/u/ibmuser)
UFSD092I 3 filesystem(s) unmounted
UFSD099I UFSD shutdown complete
```

## Recovery after Abend

If UFSD abends (S0C4, S222 from `/C`, etc.), the ESTAE handler runs in emergency mode (`UFSD_SHUT_ABEND`): it nulls the SSVT function entry and clears `UFSD_ANCHOR_ACTIVE` — stopping new SSI dispatches and letting parked clients bail — then percolates for the MVS dump. It deliberately frees **nothing**: under RTM there is no reliable way to tell whether another address space is still executing inside the router module or the CSA pools, and freeing them then is exactly the S0C4 being recovered from. SSCT, SSI router, and CSA pools are therefore left allocated and reclaimed by UFSDCLNP on the next start — this is by design, not a defect (see [CSA Retained after /C or Abend](#csa-retained-after-c-cancel-or-abend-by-design)).

**Symptoms of stale resources:**

- `/S UFSD` fails with `IEF612I PROCEDURE NOT FOUND` (JES2 proc name still locked after abend)
- A new UFSD instance starts but fails to register the SSCT ("subsystem already registered")
- Clients get S0C4 when calling `ufsnew()` (stale CSA pointers)

### Recovery Procedure

1. Run the UFSDCLNP emergency cleanup:

   ```
   /S UFSDCLNP
   ```

   Console output:

   ```
   UFSDCLNP starting -- emergency UFSD cleanup
   UFSDCLNP SSVT function pointer cleared
   UFSDCLNP SSCT deregistered and freed
   UFSDCLNP SSI router module freed
   UFSDCLNP CSA pools freed (trace + buffers + requests)
   UFSDCLNP anchor freed
   UFSDCLNP complete -- UFSD can be restarted
   ```

2. Restart UFSD:

   ```
   /S UFSD
   ```

If JES2 still refuses the proc name, wait for the old job to be purged (check `$DA` or `$DQ` for stuck entries), or use `$PJ` to purge it manually.

### UFSDCLNP Details

UFSDCLNP is a standalone program that locates the UFSD anchor in CSA via the SSCT chain, then tears down all resources:

1. Nulls the SSVT function pointer (no new SSI dispatches) and clears `UFSD_ANCHOR_ACTIVE` (a client parked in the router bails on its next timeout)
2. **Drains `anchor->inflight` to zero** — keeping the eye catcher and the router module present — so a parked client bails `RC_CORRUPT` and leaves the module *before* it is freed (best-effort, ~12 s ceiling; see below)
3. Deregisters and frees the SSCT
4. Unloads the SSI router module from CSA
5. Frees all CSA pools (trace ring, buffer pool, request pool)
6. Invalidates the eye catcher and frees the anchor

The drain (step 2) mirrors the clean-`/P` path and closes the window where freeing the router module out from under a parked client would S0C4 that client's address space (issue #39). It is **best-effort**: a count still standing after the ceiling is almost certainly leaked — a client that faulted or whose address space was cancelled while in-flight never runs its decrement — so UFSDCLNP warns (`UFSD146W … assuming leaked, freeing`) and frees anyway. Stranding CSA is the one failure UFSDCLNP exists to prevent, and it has no fallback but an IPL.

UFSDCLNP is safe to run when UFSD is not registered — it reports "nothing to do" and exits RC=0.

**Important:** Do not run UFSDCLNP while UFSD is *healthy and active*. It clears `UFSD_ANCHOR_ACTIVE` and frees the CSA, which pulls the storage out from under the running STC — an S0C4 in UFSD itself. The drain protects in-flight *clients* during recovery; it does not detect a live server. UFSDCLNP is for the post-abend case where the STC is already gone.

### Installation

Copy the UFSDCLNP STC procedure from `samplib/ufsdclnp` to `SYS2.PROCLIB(UFSDCLNP)`.

## Known Issues

### CSA Retained after /C CANCEL or Abend (by design)

After `/C UFSD` or any abend, the ESTAE handler runs in `UFSD_SHUT_ABEND` mode: it nulls the SSVT function entry and clears `UFSD_ANCHOR_ACTIVE` (so `iefssreq` stops routing new clients into UFSDSSIR and any parked client bails on its next router timeout), then percolates. It **frees nothing** — under RTM a foreign PSW may still be executing inside the router module or touching the CSA pools, and freeing them then is the very S0C4 this behaviour prevents.

So `/S UFSDCLNP` is the **designed** recovery step after `/C` or an abend, not a workaround. UFSDCLNP nulls the SSVT entry (idempotent), clears `UFSD_ANCHOR_ACTIVE`, then **drains `anchor->inflight` to zero before it frees anything** — keeping the eye catcher and the router module present throughout. A client still parked in the router revalidates the (still-valid) eye catcher on its next timeout, sees the cleared flag, and bails `RC_CORRUPT`, decrementing the counter as it leaves. Only once the count reaches zero does UFSDCLNP deregister the SSCT and free the router module, pools, and anchor (invalidating the eye catcher just before the anchor FREEMAIN). This closes the window — present before #39 — where freeing the router out from under a parked client faulted that client's address space (`S0C4`; contained by a client-side ESTAE such as HTTPD's, fatal to a bare libufs batch client).

The drain is **best-effort**, ceiling ~12 s (two `UFSD_WAIT_INTERVAL`s, so a live parked client gets two wake-and-bail chances). A count still standing after that is almost certainly leaked — a client that faulted or was cancelled while in-flight never runs its decrement — so UFSDCLNP warns (`UFSD146W`) and frees anyway rather than strand the CSA. So a genuinely stuck worker does not hang the cleanup indefinitely; worst case UFSDCLNP waits ~12 s, then reclaims.

A clean `/P UFSD` (or `/F UFSD,SHUTDOWN`) runs in `UFSD_SHUT_NORMAL` mode instead: it nulls the SSVT entry, clears `UFSD_ANCHOR_ACTIVE`, then **drains** `anchor->inflight` to zero and frees the CSA itself — no UFSDCLNP needed after a normal stop. If the drain does not reach zero within its ceiling (~10 s), UFSD issues `UFSD098W` and retains the CSA rather than freeing storage a client may still be executing; run `/S UFSDCLNP` in that case.

**TSK-71** (ESTAE handler "incomplete" after `/C`) is closed by this: the retained CSA is intended, and UFSDCLNP is the supported recovery path.

### No Per-File Permission System

Access control is mount-level only (RO/RW mode + OWNER restriction). The `mode` bits stored in inodes are informational only and not enforced by UFSD. Full RACF/RAKF integration with per-operation RACHECK is planned for a future release.

### Single-Threaded Dispatch

The UFSD dispatch loop is single-threaded. Slow BDAM I/O on one request blocks all other clients. This is acceptable for single-user or low-load scenarios. Multi-worker support (pre-ATTACHed worker pool) is planned for a future release.

### File Size Limit

Maximum file size is 4.06 MB (16 direct blocks + 1024 single indirect blocks at 4K block size). Double and triple indirect blocks are not implemented. This is sufficient for web content (HTML, CSS, JS).

### No File Locking

Concurrent writes to the same file from different sessions are not serialized. Applications must coordinate access externally.

## WTO Message Reference

All UFSD messages follow the `UFSDnnnS` pattern, where `nnn` is the message number and `S` is the severity (I=informational, W=warning, E=error).

### Startup and Shutdown (000–099)

| Message | Severity | Description |
|---------|----------|-------------|
| UFSD000I | I | UFSD starting |
| UFSD001I | I | Version banner with CSA summary |
| UFSD040I | I | Mount summary (n filesystems mounted) |
| UFSD041I | I | Individual mount detail (path, DSN, mode, owner) |
| UFSD090I | I | Shutdown initiated |
| UFSD091I | I | Superblock written for disk |
| UFSD092I | I | Filesystems unmounted |
| UFSD096I | I | CSA freed |
| UFSD097E | E | Cannot enter supervisor state at shutdown — CSA retained, run UFSDCLNP |
| UFSD097W | W | Abend shutdown — CSA retained, run UFSDCLNP before restart |
| UFSD098E | E | Abend intercepted — emergency shutdown |
| UFSD098W | W | Client(s) still in flight at shutdown — CSA retained, run UFSDCLNP |
| UFSD099I | I | Shutdown complete |

### Configuration and Mounts (060–079, 120–129)

| Message | Severity | Description |
|---------|----------|-------------|
| UFSD061E | E | Parmlib not found |
| UFSD062I | I | Parmlib config dump (ROOT/MOUNT listing) |
| UFSD076I | I | Inode cache refill complete |
| UFSD122W | W | Cannot create mount point |
| UFSD123W | W | Cannot mount dataset |

### Sessions and Operations (100–119)

| Message | Severity | Description |
|---------|----------|-------------|
| UFSD100I | I | Session opened |
| UFSD101I | I | Session closed |
| UFSD110I | I | Sessions list (from /F UFSD,SESSIONS) |
| UFSD111I | I | Session pruned (stale ASID) |

### UFSDCLNP (140–149)

| Message | Severity | Description |
|---------|----------|-------------|
| UFSD140I | I | UFSDCLNP starting |
| UFSD141I | I | SSVT function pointer cleared |
| UFSD142I | I | SSCT deregistered and freed |
| UFSD143I | I | SSI router module freed |
| UFSD144I | I | CSA pools freed |
| UFSD145I | I | Anchor freed |
| UFSD146W | W | Client(s) still in flight after the drain ceiling — assumed leaked, CSA freed anyway |
| UFSD149I | I | UFSDCLNP complete |

> **Note:** The message numbers listed here reflect the current codebase. Exact numbers may differ slightly — consult the source for authoritative message IDs.
