# AP-1d: Session Management — DONE

Sessions work. UFS disks mounted in STC.

## What was built

**Step 1 — Session infrastructure:**
- `ufsd#ses.c`: session table (64 slots, STC heap), token scheme `((slot+1)<<16)|(serial&0xFFFF)`
- `ufsd#que.c`: UFSREQ_SESS_OPEN + UFSREQ_SESS_CLOSE dispatch
- `ufsd#cmd.c`: SESSIONS command (`/F UFSD,SESSIONS`)
- `client/ufsdtst.c`: UFSDTEST test client

**Step 2 — UFS disk integration:**
- `ufsd#ini.c`: TIOT scan for UFSDISK0-9, `osddcb`/`osdopen`/`__rdjfcb`, boot block validation, `UFSD_DISK_ROOT` flag
- `ufsd#ses.c`: `ufsd_sess_open` allocates `UFSD_UFS` handle (cwd="/"); `ufsd_sess_close` frees it
- JCL proc: UFSDISK0/UFSDISK1 DD cards with DISP=OLD

## Verified output
```
UFSD040I 2 disk(s) mounted
UFSD041I   UFSDISK0 DSN=IBMUSER.UFSD.UFSDISK0 (root)
UFSD041I   UFSDISK1 DSN=IBMUSER.UFSD.UFSDISK1
UFSTST01I Session opened, token=0x00010001
UFSTST02I Session closed
```

## Key files
- `src/ufsd#ses.c` — session lifecycle
- `src/ufsd#ini.c` — disk initialization
- `client/ufsdtst.c` — test client
