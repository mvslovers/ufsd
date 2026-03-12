# AP-1b: CSA Infrastructure + SSCT — DONE

CSA structures allocated, SSCT registered in JESCT chain, clean shutdown.

## What was built
- GETMAIN SP=241: UFSD_ANCHOR, request pool (32 blocks), buffer pool (16×4K), trace ring (256 entries)
- SSCT + SSCVT in CSA, chained into JESCT
- SSI function routine set to dummy (RC=8)
- Clean shutdown: SSCT deregistered, all CSA freed
- STATS command shows CSA allocations

## Key files
- `src/ufsd#csa.c` — CSA allocation, pool management
- `src/ufsd#ssc.c` — SSCT/SSCVT registration
- `src/ufsd#trc.c` — trace ring buffer
- `include/ufsd.h` — all control block definitions
