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
| ufsdssir WAITs for reply | `WAIT ECB=(&local_ecb)` | problem | key-8 local ECB |
| STC wakes client | `__xmpost(client_ascb, client_ecb_ptr, 0)` | supervisor | inside key-0 window |
| client_ecb location | local stack var in ufsdssir | key-8 | **NOT** in CSA |
| server_ecb location | `anchor->server_ecb` in CSA | key-0 | STC WAITs in supervisor |

---

## Abend 1: S047 — WAIT protection error

**Symptom:** UFSDSSIR called `ecb_wait()` on `anchor->server_ecb` (CSA, key 0).
Unauthorised task cannot WAIT on a key-0 ECB.

**Fix:** Use a client-private ECB on the stack (key 8). UFSDSSIR WAITs on the
client's own ECB via inline `WAIT ECB=(%0)`.

## Abend 2: S0C4 / INTC=0x0010 — segment translation at ufsdssir entry

**Root cause:** crent370's `iefssreq` passes R1 = SSOB address directly (MVS SSI
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

While UFSD is registered, `S UFSD` fails with IEF612I PROCEDURE NOT FOUND.
MVS routes system-internal SSI calls (job-step notifications) through ufsdssir
which rejects them. After `/P UFSD` (deregisters SSCT), `S UFSD` works again.

If UFSD abends without cleanup, SSCT remains registered until IPL. ESTAE exit
is mandatory (see concept.md section 4.8).
