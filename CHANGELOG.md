# Changelog

UFSD is a virtual file system server for MVS 3.8j: a started task that owns
the disks of a Unix-like file system and serves it to programs in other
address spaces through the `libufs` client library.

Earlier releases are described on their GitHub release pages.

## 1.5.2 (not yet released)

A release of one fix. Nothing is added to the interface.

### Read this first

- **Run `F UFSD,REBUILD` once after the upgrade.** Earlier releases could give
  one block of the disk to two files, or twice to the same file (see the table
  below). This release stops that, but it does not undo it: a file system that
  has such a block still has it. When a file that owns a shared block is
  deleted, the block goes back to the free list although another file still
  uses it, and deleting the other file frees it a second time. The free list
  then holds a block that is in use, or the same block twice, and a later
  write overwrites a live file. `F UFSD,REBUILD` builds the free list again
  from the blocks that the files really use, so a block in use is not handed
  out. It scans every read/write file system and holds up all clients while it
  runs, so choose a quiet moment.
- **Files that already share a block stay damaged.** The second writer
  overwrote the block, so one of the two files holds the other one's data in
  that block (4096 bytes on a disk with 4096-byte blocks). UFSD cannot tell
  which files are affected: compare the files with a copy, or write them again
  after the upgrade.

### Fixed

| Before | Now |
|---|---|
| When the server wrote a file and its list of free blocks ran out, it rebuilt the list by scanning the inodes on the disk. The scan counted only the blocks that an inode on the disk points to. But a file being written keeps its new blocks in memory until the write call ends, and the block just taken is in no inode at all. The scan listed those blocks as free, and a block taken soon after was one of them: two files, or two blocks of one file, shared a block, and nothing reported an error. In a test with 4096-byte blocks, about one file in twenty of 32 KB came back with one wrong block. The scan runs when the list cannot be refilled from the chain of free blocks on the disk: when that chain is missing or broken, for instance after earlier releases ran on the disk, and after blocks have been freed. | The scan is told which blocks are taken but not yet in an inode on the disk: the block just taken, and the blocks that the write has assigned so far. This holds when the write starts with an empty list and for any number of blocks in one call. A new indirect block is written to the disk, zeroed, before the first block that it lists is taken, because the scan reads an indirect block from the disk. |
| A write that needed more blocks than were free could report success after writing over blocks that it had taken itself. | It stops with return code 44 (no space) after writing exactly the blocks that were free. |

The fix was tested by writing 200 files of 32 KB through `libufs` and reading
every block back, on disks with 4096-byte and with 512-byte blocks (a call of
4096 bytes then takes eight blocks), with calls that start in the middle of a
block, and with and without the chain of free blocks. Before the fix up to
2,108 of 12,800 blocks came back wrong and up to 32 blocks were used by two
inodes; with the fix none, and the free list never holds a block in use.

## 1.5.1 (not yet released)

A release of fixes. Nothing is added to the interface.

### Read this first

- **Upgrading from 1.5.0.** Stop UFSD, run the install job, start UFSD. The
  common-storage control block of the server (the anchor) has a new layout
  in this release, so the server `UFSD`, the router `UFSDSSIR` and the utility
  `UFSDCLNP` must all come from 1.5.1. The install job replaces all three; do
  not copy single modules between releases: router and server now share the
  request queue by a different rule, so a mixed pair would not agree on where
  the queue ends. If an earlier server ended abnormally and left its storage
  behind, the 1.5.1 start reclaims it; this
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
| The pools of request blocks and 4K buffers in common storage were taken from with a compare-and-swap on the head of the free chain after reading the next block. If another processor took that block, took the next one and put the first back in between, the swap still succeeded and the same block had two owners. The case follows from reading the code. A stress run with two processors and two clients, taken before this fix and with the queue fix applied, returned foreign and corrupt data and 1,076 server errors; with this fix it returned none. The cause was not confirmed in a dump. | Both pools change their head only with a double-word compare-and-swap on the head and a counter, so a stale swap is refused. |
| With two processors and two clients running at the same time, a request could be lost in the queue between the router and the server, and its client then waited forever. The router linked a new request behind the last one in the queue while the server was taking that last one out; the new request ended up behind a block the server already had, and the server never saw it. This was observed: in a stress test with two processors and two clients, all four runs (5 and 30 minutes) stopped within a few minutes. | The queue no longer has a window in which an add and a take can disagree. Clients add a request with one compare-and-swap; the server takes the whole chain with one and serves it in the order the requests arrived. The same stress test then ran without a lost request and without an error: 30 minutes with 713,804 requests, and 5 minutes with 100,004 requests on the final build. More than two processors and runs longer than 30 minutes were not tried. |
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
