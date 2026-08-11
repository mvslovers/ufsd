# Cross-Address-Space Communication Reference (MVS 3.8j / S370)

Hard-won constraints from UFSD AP-1c implementation. Five successive abends,
each documented with symptom, root cause, fix. This is the authoritative
reference for cross-AS mechanics on MVS 3.8j under Hercules.

---

## Summary Table

| Operation | Mechanism | Required State | Wrong approach and Abend |
|-----------|-----------|----------------|--------------------------|
| POST cross-AS | `__xmpost(ascb, ecb_ptr, code)` via CVT0PT01 | Supervisor (key 0) | `ecb_post` (SVC 2) from problem state: S102 |
| POST from supervisor | Not via SVC 2 | n/a | SVC 2 from supervisor state: S202 |
| WAIT on ECB | `WAIT ECB=(addr)` SVC 1 | Problem state | Key-0 ECB from problem state: X'201' |
| SSI routine entry | R1 = SSOB (MVS convention) | Supervisor | C plist dereference: S0C4 |

## Final Working Design

| Operation | Mechanism | State | Notes |
|-----------|-----------|-------|-------|
| ufsdssir wakes STC | `__xmpost(server_ascb, &server_ecb, 0)` | supervisor | before `__prob` |
| ufsdssir WAITs for reply | `ecb_timed_wait(&local_ecb, …)` retry loop | problem | key-8 local ECB; liveness-checked (see below) |
| STC wakes client | `__xmpost(client_ascb, client_ecb_ptr, 0)` | supervisor | inside key-0 window |
| client_ecb location | local stack var in ufsdssir | key-8 | **NOT** in CSA |
| server_ecb location | `anchor->server_ecb` in CSA | key-0 | STC WAITs in supervisor |

---

## Reply-Wait Liveness and In-Flight Drain (post-AP-1c)

The plain blocking `WAIT` above was later replaced by a timed,
liveness-checked wait so a client parked in `ufsdssir` cannot hang
forever if the STC shuts down or abends while its request is
outstanding. The cross-AS state/key rules are unchanged — only the WAIT
mechanism evolved (`src/ufsd#ssi.c`).

**Timed wait loop.** Instead of one blocking `WAIT`, the router loops on
`ecb_timed_wait(ecbp, UFSD_WAIT_INTERVAL, UFSD_TIMEOUT_CODE)`, where
`UFSD_WAIT_INTERVAL` = 500 hundredths (5 s) and `UFSD_TIMEOUT_CODE` =
X'0FFFF' is a sentinel post code, chosen distinct from the STC's normal
reply (post code 0). A normal reply
(`(*ecbp & ECB_VALUE_MASK) != UFSD_TIMEOUT_CODE`) breaks the loop; a timer
pop drives the liveness check. Still key-8 local ECB, still in problem
state — only the SVC 1 `WAIT` became a timed `STIMER`+`WAIT`.

**Liveness check on timeout.** The router revalidates the anchor eye
catcher (`memcmp(anchor->eye, "UFSDANCR", 8)`) and re-tests
`UFSD_ANCHOR_ACTIVE`. Freed CSA (SP=241) is reused, not zeroed, so the
flag alone cannot be trusted after an emergency shutdown — a stale high
bit would loop forever on an ECB nobody will post, so the eye catcher is
revalidated first.
- Eye valid **and** ACTIVE set: server alive, no reply yet — clear the
  ECB and re-issue the timed wait.
- Server gone or quiescing: return `UFSD_RC_CORRUPT`. If the eye catcher
  is still valid (a clean shutdown retains the anchor), give the
  in-flight count back first — open a dedicated key-0 window and
  `__udec(&anchor->inflight)` — so the drain can complete. If the eye
  catcher is gone (anchor already freed by the emergency path / UFSDCLNP),
  the counter no longer exists: do NOT write to it.

**In-flight drain protocol** (`anchor->inflight`, key-0 CSA). Every
client executing inside `ufsdssir` is counted: `__uinc(&anchor->inflight)`
right after `free_pop` (inside the entry key-0 window), and
`__udec(&anchor->inflight)` on every exit path from the router. Shutdown
(`ufsd_shutdown`) and the emergency cleanup task (UFSDCLNP) clear
`UFSD_ANCHOR_ACTIVE`, then drain `inflight` to zero before freeing the
SSI router module and the CSA pools. This quiesces in-flight clients so
CSA is never freed out from under a request still copying its result. A
drain that times out means "retain CSA" rather than risk freeing storage
a client may still touch.

---

## Abend 1: S047 — WAIT protection error

**Symptom:** UFSDSSIR called `ecb_wait()` on `anchor->server_ecb` (CSA, key 0).
Unauthorised task cannot WAIT on a key-0 ECB.

**Fix:** Use a client-private ECB on the stack (key 8). UFSDSSIR WAITs on the
client's own ECB via inline `WAIT ECB=(%0)`.

## Abend 2: S0C4 / INTC=0x0010 — segment translation at ufsdssir entry

**Root cause:** libc370's `iefssreq` passes R1 = SSOB address directly (MVS SSI
convention). C calling convention expects R1 = pointer to parameter list.
Declaring `ufsdssir(SSOB *ssob)` caused dereference of raw SSOB as C plist.

Also: `UFSSSOB.client_ecb` was at offset +12 where `iefssreq.c` reads the SSOB
individual block pointer — so `iefssreq` handed `&ping_ecb` as the SSOB address.

**Fix:** Declare `void ufsdssir(void)`. Extract SSOB via inline asm:
```c
__asm__ __volatile__("LR %0,1" : "=r"(ssob));
```
Move `UFSSSOB.client_ecb` to end of struct (after `data[]`).

## Abend 3: S202 — SVC 2 POST from supervisor state

**Symptom:** `ecb_post(&anchor->server_ecb, 0)` from within `__super/__prob`
block. POST (SVC 2) from supervisor state is not permitted.

**Fix:** Split the supervisor-state window. (Later replaced entirely by
`__xmpost` — see Abend 4.)

## Abend 4: S102 — cross-AS POST via SVC 2

**Symptom:** `ecb_post` (SVC 2) from problem state for cross-address-space POST.
Affects both directions: ufsdssir to STC and STC to ufsdssir.

**Root cause:** SVC 2 cannot post an ECB whose waiting task is in a different
address space from problem state without special cross-memory authority.

**Fix:** Replace with `__xmpost` (CVT0PT01 branch entry) in both places:
- ufsdssir wakes STC: `__xmpost(anchor->server_ascb, &anchor->server_ecb, 0)` from supervisor state
- STC wakes client: `__xmpost(req->client_ascb, req->client_ecb_ptr, 0)` from supervisor state

Added `void *server_ascb` to `UFSD_ANCHOR` (set at STC startup via `__ascb(0)`).
`req->client_ascb` set in ufsdssir via `__ascb(0)` (captures client ASCB).

## Abend 5: X'201' — WAIT on key-0 ECB from problem state

**Symptom:** After fixing cross-AS POST, UFSDPING abended X'201'. Dump analysis:
- PSW at abend: problem state, key 8
- ECB address in SVRB R1: CSA address (SP=241, key 0)
- X'201' = WAIT (SVC 1) issued from problem state for an ECB in key-0 storage

**Root cause:** ECB was stored inside UFSREQ block (CSA, SP=241, key 0). WAIT
from problem state on key-0 ECB is not permitted.

**Fix:** Remove `ECB client_ecb` from `UFSREQ`. Declare `ECB local_ecb` as a
local stack variable in ufsdssir (key-8 storage). Store `req->client_ecb_ptr = &local_ecb`.
WAIT on `&local_ecb` (key 8) works. `__xmpost` (CVT0PT01) posts it cross-AS.

## Known Behavior: double start

A second `S UFSD` while the server runs starts normally through JES2 and is
then refused by UFSD itself: startup's reclaim finds a predecessor whose
anchor is still flagged ACTIVE and stops with `UFSD002E UFSD ALREADY
REGISTERED AND ACTIVE`.

Until #55 this failed earlier and more confusingly, with `IEF612I PROCEDURE
NOT FOUND` — not because the router rejected anything (the long-held theory
here), but because the subsystem was named `UFSD` like the procedure, which
put the start on the master-subsystem path. The subsystem is now `UFS1`; see
`docs/recovery.md`, "Why the Subsystem is not called UFSD".

If UFSD abends, SSCT and CSA stay allocated by design — the next `/S UFSD`
reclaims them (#49), UFSDCLNP is the standalone fallback. The ESTAE exit is
still mandatory: without it the abend leaves the SSVT entry live, and
clients keep being dispatched into a router whose address space is gone
(see concept.md, "SSI Router").
