# AP-1c: SSI Router + First Round-Trip — DONE

**Critical milestone achieved:** First cross-AS request via IEFSSREQ.
`UFSPING2I UFSD responded: RC=0 (PONG)` — CC 0000 confirmed.

## What was built
- SSI thin router (ufsdssir) loaded into CSA
- CS lock-free enqueue with conditional POST bundling
- Server dequeue + dispatch in main loop
- Client test program UFSDPING (batch)
- Request validation (eye catcher + function code)
- Trace logging for every request/response

## Five abends debugged

The road to a working round-trip required root-causing five successive abends.
Each is documented in `doc/cross-as-reference.md` as a permanent reference
for MVS 3.8j cross-AS mechanics.

Key outcomes:
- `__xmpost` (CVT0PT01) instead of SVC 2 for cross-AS POST
- Client ECB as local stack variable (key-8), not in CSA (key-0)
- SSI entry: `void ufsdssir(void)` + inline asm R1 extraction
- ASCB tracking in anchor and request blocks

## Key files
- `src/ufsdssir.c` — SSI thin router
- `src/ufsd#que.c` — lock-free queue + dispatch routing
- `src/ufsd#csa.c` — request alloc/free (updated)
- `client/ufsdping.c` — ping/pong test client

## Open items (carried to AP-1f)
- ESTAE exit for abend recovery (SSCT remains registered on abend)
- APF auth in clients is temporary PoC workaround
