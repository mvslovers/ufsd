# Releasing UFSD — the SMP4 FMID

Moved here from `CLAUDE.md`. The ecosystem-wide rules (id format, burned
ids, `DELETE`, the element-ownership wall, `LIST`) are in
`~/repos/mvs/.workspace/context/smp.md`; this file holds what is particular
to ufsd. Read both before cutting a release.

The id is the minor release: `T` + three product letters + major + minor +
`0`. One id per minor, **spent exactly once**, and each release's SYSMOD
deletes its predecessor (a patch needs an explicit id of its own, below):

```toml
[smp]
prefix = "TUFS"     # 1.5.0 -> FMID TUFS150, DELETE(TUFS140)
```

**No version component may ever exceed 9** — a 7-character id has no room for
a second digit. At patch 9 cut the next minor, at minor 9 the next major;
ufsd 1.3.10 cannot be expressed and must not be released.

Current: **`TUFS150`** for 1.5.0, deleting `TUFS140`, both derived by mbt
from `[project] version` and `[smp] prefix`. The derivation covers an `x.y.0`
release only (measured with mbt 3.0.0-dev 6db906e):

- **A patch release stops the package.** For 1.4.1 mbt reports `version 1.4.1
  is a patch release: its FMID would be TUFS140 again, the minor's id, and a
  patch is a PTF, which mbt does not build yet -- set [smp] fmid (and delete)
  explicitly`. Per the rule above the patch is a function level of its own:
  set `fmid = "TUFS141"` and `delete = ["TUFS140"]`.
- **The minor after a patch needs `delete` by hand too.** 1.5.0 derives
  `DELETE(TUFS140)` whatever was released in between; after a 1.4.1 that is
  the wrong id, and a successor that does not delete the installed owner of
  `MOD(UFSD)` should install nothing at RC 00 -- the element-ownership wall in
  `~/repos/mvs/.workspace/context/smp.md`, inferred for this case, not measured. Write
  `delete = ["TUFS141"]` beside the derived `fmid`.
- **A release deletes only its immediate predecessor**, so a system that
  skipped a release still has an older owner of `MOD(UFSD)` and takes the
  same wall. 1.4.0 met it: `TUFS140` does not delete `TUFS120`, and its
  package README still described 1.3.0's path from 1.2.x (fixed after the
  release; the release page carries the correction). `docs/installation.md`
  section 10 opens with the `LIST CDS/ACDS MOD(UFSD)` check -- keep it, and
  update its "direct upgrade" owner with every release.

Burned here: `TUFS110` (1.1.x, never released but accepted on a test system),
`TUFS120` (1.2.0-1.2.2, `REC APP ACC` on mvsdev), `TUFS130` (1.3.0,
released 2026-09-14) and `TUFS140` (1.4.0, released 2026-10-13). `TUFS121`
and `TUFS122` are never assigned -- those releases shipped under `TUFS120` when the rule was one id per *minor*, so the
id space skips. `TUFS131` was assigned to 1.3.1, which was never cut -- the
libc370 2.0 port made the next release 1.4.0 -- so it is unspent and stays
unassigned. The gaps are deliberate.

Never re-spend an id, and never install a test package under the real one: a
test needs a throwaway id **and** throwaway module names, because SMP keys
element ownership on `MOD(name)`, not on the target library. See
`~/repos/mvs/.workspace/context/smp.md` for the full rule and the measurements
behind it.
