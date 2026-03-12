# AP-1a: STC Skeleton — DONE

Minimal STC that starts, accepts MODIFY/STOP commands, shuts down cleanly.
No CSA, no UFS, no subsystem.

## What was built
- STC main program (UFSD) with WAIT loop + QEDIT ORIGIN for CIB
- Command parser: STATS, SHUTDOWN, HELP
- `/P UFSD` sets QUIESCE flag, clean exit
- WTO messages with UFSD prefix
- JCL proc in SYS1.PROCLIB

## Key files
- `src/ufsd.c` — main + loop
- `src/ufsd#cmd.c` — command parser
