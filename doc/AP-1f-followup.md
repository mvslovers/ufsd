# AP-1f Follow-Up Items

Observed during HTTPD integration testing (AP-1f).
These are not blockers — AP-1f is complete and HTTPD FTP works correctly.

---

## 1. nlink = 0 in pre-existing ufs370 inodes

**Symptom:** FTP `LIST` shows `nlink = 0` for all entries in the test filesystem.

**Root cause:** The files on disk were created by the old ufs370 library.
`ufsd_ino_read` returns `edino.nlink = 0` for those inodes, which means
ufs370 never wrote a non-zero `nlink` to disk (or stored it at a different
offset that does not match `UFSD_DINODE.nlink` at offset 0x002).

**Scope:** Pre-existing filesystem data only.  Files created via the UFSD
FOPEN path have `nlink = 1` (files) or `nlink = 2` (directories) set
correctly by `do_fopen` and `do_mkdir`.

**Action:** Investigate whether the on-disk inode layout of ufs370 matches
`UFSD_DINODE` byte-for-byte.  A filesystem migration or `fsck`-style
fixup tool may be needed if the layouts differ.

---

## 2. ufs_fgets / ufs_fgetc efficiency — one UFSD request per character

**Symptom:** FTP `RETR` in ASCII mode generates one UFSD FREAD request
per character (observed: ~650 requests for a 6 KB file).

**Root cause:** `ufs_fgets` calls `ufs_fgetc` in a loop.  `ufs_fgetc`
calls `ufs_fread(ptr, 1, 1, fp)` — one SSI round-trip per byte.

**Impact:** ASCII-mode text transfers are slow.  Binary-mode transfers
use `ufs_fread(buf, 1, sizeof(buf), fp)` and are unaffected (one request
per buffer, confirmed: 6318 bytes at 110 KiB/s, RC=0).

**Fix:** Implement a read-ahead buffer inside `UFSFILE` (e.g. 512 bytes).
`ufs_fgetc` fills the buffer in one FREAD request and drains it
character by character without further SSI calls.  `ufs_fgets` can
then call `ufs_fgetc` without per-character overhead.

**Priority:** Medium — only affects ASCII-mode transfers and Lua `io.read()`
on text files.  Binary transfers are correct and fast.
