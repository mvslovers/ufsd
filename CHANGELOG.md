# Changelog

UFSD is a virtual file system server for MVS 3.8j: a started task that owns
the disks of a Unix-like file system and serves it to programs in other
address spaces through the `libufs` client library.

Earlier releases are described on their GitHub release pages.

## 1.5.1 (not yet released)

A release of fixes. Nothing is added to the interface.

### Read this first

- **Upgrading from 1.5.0.** Stop UFSD, run the install job, start UFSD. The
  common-storage control block of the server (the anchor) has a new layout
  in this release, so the server `UFSD`, the router `UFSDSSIR` and the utility
  `UFSDCLNP` must all come from 1.5.1. The install job replaces all three; do
  not copy single modules between releases: router and server now share the
  request queue by a different rule, so a mixed pair would not agree on where the queue ends.
  If an earlier server ended
  abnormally and left its storage behind, the 1.5.1 start reclaims it; this
  was tried with the storage left by a cancelled 1.4.0 server, whose anchor
  has the layout of 1.5.0. Coming from 1.4.0 (or an earlier release), the SMP install
  does not replace the modules by itself; `docs/installation.md` says what to do.
  Details: `docs/installation.md`, section 10.
- **`libufs` refuses long paths.** `ufs_chgdir`, `ufs_mkdir`, `ufs_rmdir` and
  `ufs_remove` now return `UFSD_RC_INVALID` for a path of 256 characters or
  more and send nothing. Release 1.5.0 and earlier cut such a path to 255
  characters and sent it with its 256th character in place of the terminating
  null, so the server could act on a path other than the one you named.
  Programs get this change, and the read fix below, by being rebuilt with the
  1.5.1 `libufs`.
- **Disks that already lost inodes are not repaired.** See the first entry
  under "Fixed". Only a new format with `FORCE` gives such inodes back.

### Fixed

| Before | Now |
|---|---|
| Removing a file or directory freed its inode but left the old contents on the disk. Once more than 64 inodes had been freed since the start, the server dropped the others, and neither `F UFSD,REBUILD` nor the refill of the free inode cache found them again. A program that created and removed files in a loop used up the file system's inodes, and every later create failed with return code 48. | The inode is cleared on the disk before it is given back, so the refill and `F UFSD,REBUILD` find it. |
| `ufs_fread` could report end of file early, and the file position moved ahead of the data delivered, when the pool of 4K transfer buffers in common storage was empty. | The server reserves the buffer before it reads. When none is free it answers with at most 252 bytes and moves the position by exactly what the answer carries. |
| A read that failed, and every read at the end of a file with a buffer larger than 252 bytes, leaked up to 4 KB of the server's storage. | The buffer is released on every exit. |
| The pools of request blocks and 4K buffers in common storage were taken from with a compare-and-swap on the head of the free chain after reading the next block. If another processor took that block, took the next one and put the first back in between, the swap still succeeded and the same block had two owners. The case follows from reading the code; it has not been observed. | Both pools change their head only with a double-word compare-and-swap on the head and a counter, so a stale swap is refused. |
| With two processors and two clients running at the same time, a request could be lost in the queue between the router and the server, and its client then waited for ever. The router linked a new request behind the last one in the queue while the server was taking that last one out; the new request ended up behind a block the server already had, and the server never saw it. This was observed: in a stress test with two processors and two clients, all four runs (5 and 30 minutes) stopped within a few minutes. | The queue no longer has a window in which an add and a take can disagree. Clients add a request with one compare-and-swap; the server takes the whole chain with one and serves it in the order the requests arrived. The same stress test then ran without a lost request and without an error: 30 minutes with 713,804 requests, and 5 minutes with 100,004 requests on the final build. More than two processors and runs longer than 30 minutes were not tried. |
| The server trusted the byte count of a write request and the end of a path string in a request. `libufs` never sends a request that violates either; another caller of the subsystem interface could. | A count larger than the request holds, and a path without a terminating null inside the request, are refused with `UFSD_RC_INVALID`. |

### Changed

- The console message `UFSD097E` reads `CANNOT ENTER SUPERVISOR STATE -- CSA
  RETAINED, RECLAIMED AT NEXT START`. It used to tell the operator to run
  `UFSDCLNP` before the next start. The next start reclaims the storage itself
  unless it answers with `UFSD155W` or `UFSD156W`; only after `UFSD156W` is
  `UFSDCLNP` needed.

### Added

- The `LICENSE` file (MIT License). The README linked to it before the file
  existed.
- The manuals _UFSD User's Guide_ (ML05-0001) and _UFSD Reference_ (ML05-0002)
  are brought up to this release: the upgrade from 1.5.0 and from 1.4.0, the
  removal, the start and recovery screens, the limits and the library
  chapters. They are still Draft editions. Their sources are in `docs/books`;
  the PDFs are attached to the release.
