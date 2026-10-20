# ufsd — Virtual Filesystem Daemon for MVS 3.8j

Cross-address-space filesystem server (STC/subsystem) for MVS 3.8j.  
Derived from [ufs370](https://github.com/mvslovers/ufs370). Runs on Hercules.

Released; the version on `main` is `[project] version` in `mbt.toml`.
Phase history and plan: `internals/concept.md` (changes table, section 14).

## Architecture in 30 Seconds

UFSD is an MVS Started Task (STC) that owns all BDAM disk I/O. Clients
(HTTPD, batch, TSO) communicate via IEFSSREQ (SVC 34) through an SSI
thin router. Requests flow through a lock-free CSA queue (CS instruction).
Cross-AS POST uses `__xmpost` (CVT0PT01), not SVC 2. Client ECBs live on
the caller's stack (key-8), not in CSA (key-0).

UFSD reimplements disk I/O directly (not linking ufs370 library) because
`__wsaget()` and DCB management in ufs370 are address-space-local.

## Read when relevant

| Before you … | read |
|---|---|
| answer an architecture question, change a control block or the design | `internals/concept.md` |
| touch cross-AS POST/WAIT/ECB code | `internals/cross-as-reference.md` |
| cut a release, touch `[smp]`, pick an FMID | `internals/releasing.md`, then `~/repos/mvs/.workspace/context/smp.md` |
| change the upgrade or removal path | `docs/installation.md` section 10, `docs/uninstall.md` |

## Critical MVS 3.8j Constraints

These are hard-won from implementation. Violating them causes abends:

1. **Cross-AS POST:** Use `__xmpost(ascb, ecb_ptr, code)` from supervisor state.
   `ecb_post` (SVC 2) → S102 cross-AS. `POST` from supervisor → S202.
2. **Client ECB:** Must be local stack variable (key-8). CSA ECB (key-0) → X'201'.
3. **SSI entry:** `void ufsdssir(void)` — extract R1=SSOB via inline asm.
   C plist convention does NOT apply. R1 is raw SSOB pointer.
4. **ESTAE is mandatory.** Without it, abend leaves SSCT registered → blocks restart until IPL.
5. **CSA writes from dispatch functions:** Never. All CSA writes go through
   `ufsd_dispatch`'s key-0 window. Dispatch functions write to stack-local `resp_data[]`.
6. **4K buffer pattern:** `do_fread`/`do_fwrite` use heap staging buffers.
   `ufsd_dispatch` copies to/from CSA UFSBUF in key-0. The staging pointer is
   passed via `resp_data[4..7]`.
7. **No writable statics in an AC(1) module.** UFSD/UFSDSSIR/UFSDCLNP are
   link-edited AC(1); from an APF-authorized library program fetch takes the
   job pack area in subpool 252 **key 0**, while the STC runs key 8 — a store
   into a C static or non-const global then abends S0C4 (#64). SVC 244 only
   sets `JSCBAUTH` afterwards and changes nothing about the storage key. Put
   the value in `UFSD_STC` (a `main()` local, key 8, reachable through
   `anchor->server_stc`), on the heap, or in CSA behind the key-0 window.
   `tools/check-module-data.py` enforces this and runs in CI.

## Build

mbt 3, driven by `mbt.toml`; the whole build runs on the host with cc370:
```
mbt build --all     # load modules + libufs.a
mbt test            # host tests (TSTUFSAV, TSTUFSG, TSTUFSMP)
mbt package         # dist/: load + lib archives, SMP install package
mbt deploy          # -> UFSD.DEV.LINKLIB (touches MVS)
```
`python3 tools/check-module-data.py` checks constraint 7 against `mbt.toml`.

**libufs host tests:** `client/libufs.c` builds on the host unchanged; the test
supplies its own `iefssreq()` (the only MVS call) and scripts the answers. To
add a case, extend `test/host/tstlibuf.c` (helpers `s_transport`, `s_refused`,
`s_rc`, `s_resp`, ...); the libc370/IBM headers it needs are type-only stand-ins
in `test/host/include/`.

## Module Map

| Module | File | Purpose |
|--------|------|---------|
| UFSD | `src/ufsd.c` | STC main + main loop + ESTAE |
| UFSD#CMD | `src/ufsd#cmd.c` | MODIFY command parser |
| UFSD#CFG | `src/ufsd#cfg.c` | PARMLIB configuration parser |
| UFSD#CSA | `src/ufsd#csa.c` | CSA pools (request + trace) |
| UFSD#BUF | `src/ufsd#buf.c` | CSA buffer pool alloc/free |
| UFSD#SCT | `src/ufsd#sct.c` | SSCT registration |
| UFSDSSIR | `src/ufsd#ssi.c` | SSI thin router |
| UFSD#QUE | `src/ufsd#que.c` | Lock-free queue + dispatch |
| UFSD#SES | `src/ufsd#ses.c` | Session management |
| UFSD#INI | `src/ufsd#ini.c` | Disk init (TIOT, BDAM) |
| UFSD#BLK | `src/ufsd#blk.c` | BDAM block I/O |
| UFSD#SBL | `src/ufsd#sbl.c` | Superblock + alloc |
| UFSD#INO | `src/ufsd#ino.c` | Inode I/O |
| UFSD#DIR | `src/ufsd#dir.c` | Directory ops |
| UFSD#FIL | `src/ufsd#fil.c` | File dispatch |
| UFSD#GFT | `src/ufsd#gft.c` | Global file table |
| UFSD#TRC | `src/ufsd#trc.c` | Trace ring buffer |
| UFSD#RCL | `src/ufsd#rcl.c` | Orphan reclaim + liveness guard (startup + UFSDCLNP) |
| UFSD#ASV | `src/ufsd#asv.c` | ASVT membership scan (portable, host-tested) |
| UFSD#MNT | `src/ufsd#mnt.c` | Mount point table (portable, host-tested) |
| UFSDCLNP | `src/ufsdclnp.c` | Emergency cleanup utility (batch, AC(1)) |
| UFSFMT | `src/ufsfmt.c`, `src/ufsfmtg.c` | Disk format utility (batch); `ufsfmtg.c` is the portable geometry half |

Client: `client/libufs.c` (stub library — includes ufs_stat), `client/libufstst.c` (integration test)

## Coding Rules

- Comments and documentation in English
- Module names: 8-char MVS member convention (`ufsd#xxx.c`)
- Eye catchers on every control block (8 bytes)
- All CSA structures: GETMAIN SP=241, key-0
- Every request validated before dispatch (eye catcher + func + session)
- WTO messages: `UFSDnnnX` format (nnn=number, X=severity I/W/E)
- Dispatch functions (ufsd#fil.c) NEVER write CSA directly — use resp_data[]
- 4K data transfers use staging buffers (heap) — ufsd_dispatch copies to/from CSA
- UFSFILE in libufs: ~8K per handle (4K rbuf + 4K wbuf) — document if this grows
- No mutable `static`/global at any scope in an AC(1) module — see constraint 7

## SMP4 FMID

`[smp] prefix = "TUFS"`; mbt derives the FMID and its `DELETE` from the
version (1.5.0 -> `TUFS150`, deleting `TUFS140`). A patch release, and the
minor after one, need explicit `fmid`/`delete` -- read `internals/releasing.md`
before any release.
