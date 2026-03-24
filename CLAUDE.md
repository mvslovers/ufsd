# ufsd — Virtual Filesystem Daemon for MVS 3.8j

Cross-address-space filesystem server (STC/subsystem) for MVS 3.8j.  
Derived from [ufs370](https://github.com/mvslovers/ufs370). Runs on Hercules.

## Current Status

**Post-PoC Hardening — AP-2a**  
Phase 1 (AP-1a–1f) complete. HTTPD and FTPD operational via libufsd.  
Current focus: FWRITE 4K path, timestamps, POST-Bündelung, write-behind buffer.

## Architecture in 30 Seconds

UFSD is an MVS Started Task (STC) that owns all BDAM disk I/O. Clients
(HTTPD, batch, TSO) communicate via IEFSSREQ (SVC 34) through an SSI
thin router. Requests flow through a lock-free CSA queue (CS instruction).
Cross-AS POST uses `__xmpost` (CVT0PT01), not SVC 2. Client ECBs live on
the caller's stack (key-8), not in CSA (key-0).

UFSD reimplements disk I/O directly (not linking ufs370 library) because
`__wsaget()` and DCB management in ufs370 are address-space-local.

## Key Documents

Read the relevant doc **before** making changes:

| Document | When to read |
|----------|-------------|
| `doc/AP-2a.md` | **Always** — current work package scope and deliverables |
| `doc/concept.md` | Architecture questions, control block layouts, design rationale |
| `doc/cross-as-reference.md` | Cross-AS mechanics (POST/WAIT/ECB constraints on MVS 3.8j) |
| `doc/completed/AP-1*.md` | Only if you need history on a specific completed package |

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

## Build

Same pipeline as all mvslovers projects:
```
make                # C → c2asm370 → mvsasm (via mvsMF API)
make link           # mvslink on MVS
```

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
