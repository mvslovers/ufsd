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

Current: **`TUFS151`** for 1.5.1, deleting `TUFS150`. 1.5.1 is a patch, so
its `mbt.toml` sets both explicitly (`fmid = "TUFS151"`, `delete =
["TUFS150"]`); it was cut on the branch `release-1.5.x`, which carries those
two lines and `version = "1.5.1"`. `main` stays on `1.6.0-dev` and has neither
line. The derivation from `[project] version` and `[smp] prefix` (1.5.0 gave
`TUFS150`, deleting `TUFS140`) covers an `x.y.0` release only (measured with
mbt 3.0.0-dev 6db906e):

- **A patch release stops the package.** For 1.4.1 mbt reports `version 1.4.1
  is a patch release: its FMID would be TUFS140 again, the minor's id, and a
  patch is a PTF, which mbt does not build yet -- set [smp] fmid (and delete)
  explicitly`. Per the rule above the patch is a function level of its own:
  set `fmid = "TUFS141"` and `delete = ["TUFS140"]`.
- **The minor after a patch needs `delete` by hand too.** An `x.y.0` derives
  `DELETE` of the previous minor's id whatever was released in between, so
  1.6.0 would delete `TUFS150`, while `TUFS151` is the installed owner. Cut
  1.6.0 with `delete = ["TUFS151"]` beside the derived `fmid`. A successor
  that does not delete the installed owner of `MOD(UFSD)` should install
  nothing at RC 00 -- the element-ownership wall in
  `~/repos/mvs/.workspace/context/smp.md`, inferred for this case, not
  measured.
- **A release deletes only its immediate predecessor**, so a system that
  skipped a release still has an older owner of `MOD(UFSD)` and takes the
  same wall. 1.4.0 met it: `TUFS140` does not delete `TUFS120`, and its
  package README still described 1.3.0's path from 1.2.x (fixed after the
  release; the release page carries the correction). `docs/installation.md`
  section 10 opens with the `LIST CDS/ACDS MOD(UFSD)` check -- keep it, and
  update its "direct upgrade" owner with every release (it names `TUFS150`
  while 1.5.1 is the current release; 1.6.0 makes it `TUFS151`).

Burned here: `TUFS110` (1.1.x, never released but accepted on a test system),
`TUFS120` (1.2.0-1.2.2, `REC APP ACC` on mvsdev), `TUFS130` (1.3.0,
released 2026-09-14), `TUFS140` (1.4.0, released 2026-10-13), `TUFS150` (1.5.0,
released 2026-10-20) and `TUFS151` (1.5.1, released 2026-10-26). `TUFS121`
and `TUFS122` are never assigned -- those releases shipped under `TUFS120` when the rule was one id per *minor*, so the
id space skips. `TUFS131` was assigned to 1.3.1, which was never cut -- the
libc370 2.0 port made the next release 1.4.0 -- so it is unspent and stays
unassigned. The gaps are deliberate.

Never re-spend an id, and never install a test package under the real one: a
test needs a throwaway id **and** throwaway module names, because SMP keys
element ownership on `MOD(name)`, not on the target library. See
`~/repos/mvs/.workspace/context/smp.md` for the full rule and the measurements
behind it.

## The manuals (ML05-0001, ML05-0002)

The User's Guide and the Reference are in `docs/books/` (Typst, the
`bookmaster` submodule). Release 1.5.0 shipped without them; they were made
afterwards and attached to the existing release.

**Before the release PR:**

1. The edition notice of both books (`ml05-0001.typ`, `ml05-0002.typ`, "This
   edition applies to ...") names the release whose behaviour the text
   describes.
2. Messages, commands, configuration parameters and limits are written from
   the source (`apx-messages`, `ref-cmd`, `ref-cfg`, `ref-lim`). A PR that
   changed one of them has updated the book in the same PR or named the stale
   section in its body.
3. Screens under `docs/books/ex/` are real output of a run in a throwaway
   container (`--dist tk5`, the release installed with SMP), never typed by
   hand. A changed message needs a new capture.
4. `make -C docs/books` and `sh docs/books/web/build.sh site` build without
   an error. The `books` workflow does the first on every PR.
5. `docs/installation.md` (the package README) shows the start console of
   one release in section 8 and the FMID ids in sections 10 and 11, and
   `docs/uninstall.md` the FMID table, the `DELETE` list of the dummy function
   `TUFSDEL` and the `UCLIN` jobs (also the book chapter `ug-remove.typ`).
   Replace the console with the capture in `docs/books/ex/ug-ops/start.txt`
   and add the new id to the jobs.

**At the release:** `release.yml` has a `books` job (`needs: release`). It
builds the PDFs from the tag with `SOURCE_DATE_EPOCH=0`, so a rebuild gives
the same bytes, and attaches `ml05-0001-<edition>.pdf`,
`ml05-0002-<edition>.pdf` and their lines in `SHA256SUMS`. Check the lines
with `sha256sum -c` on the downloaded assets, as for the other assets.

**A release that exists already** (1.5.0 was the first): build the PDFs from
the tag in a clean checkout with `SOURCE_DATE_EPOCH=0 make -C docs/books`,
then `gh release upload vX.Y.Z --clobber <the two PDFs> SHA256SUMS`. A release
page is final, so this needs the maintainer's approval first.

**Read the Docs** builds the web form from the default branch
(`.readthedocs.yaml`, the same sources). The project is registered by the
maintainer; a change of the build commands is a change of that file.
