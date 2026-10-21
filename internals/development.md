# UFSD — Development Guide

This document covers building UFSD from source, the server architecture, the complete libufs API reference, and assembler calling conventions.

## Building from Source

### Prerequisites

- Linux or macOS build host
- The `cc370` cross-toolchain (`cc370`, `as370`, `ld370`, `ar370`, `xmit370`) and the libc370 sysroot, installed
- [mbt 3](https://github.com/mvslovers/mbt/releases) on `PATH` — a single program, installed once per machine, not per project
- MVS 3.8j system running the [mvsMF](https://github.com/mvslovers/mvsmf) REST API (port 1080 by default) — only needed for `mbt deploy` and `mbt test --mvs`; the build itself runs entirely on the host

### Build Commands

```sh
git clone https://github.com/mvslovers/ufsd
cd ufsd
mbt doctor --offline     # check the toolchain and the sysroot
```

Then build:

```sh
mbt deps          # download + stage declared dependencies (none for ufsd today)
mbt build         # build the load modules on the host
mbt build --all   # load modules + the libufs static archive (build/libufs.a)
mbt package       # produce distribution archives in dist/
mbt deploy        # XMIT + upload the load modules into UFSD.DEV.LINKLIB
```

The entire build runs on the host with the `cc370` toolchain (`cc370` → `.o`,
`as370`, `ld370`, `ar370`); MVS is touched only by `mbt deploy` and
`mbt test --mvs`. The load modules are written locally under `build/` (e.g.
`build/UFSD.iebcopy`) and reach MVS only after `mbt deploy`. The build is driven
by `mbt.toml` (project metadata, toolchain, modules, tests, distribution);
`[toolchain] mbt` names the mbt release it is built with, and a newer installed
mbt switches to that one by itself. `libc370` is the cc370 sysroot (`-lc`), not a
declared dependency; `ufsd` itself currently declares no `[dependencies]`, so
`mbt deps` is a no-op until one is added.

The MVS system `mbt deploy` and `mbt test --mvs` talk to is a *target*, kept once
per machine in `~/.mbt/targets.toml` rather than in the project. Create one from
an existing mbt 2 `.env` with `mbt target import .env --name <name>`, check it with
`mbt target info <name>`, and pick it per run with `--target <name>`.

Other commands: `mbt build NAME` (one module), the test commands (see
[Testing](#testing)), `mbt doctor` (toolchain + MVS connectivity), `mbt compiledb`
(write `compile_commands.json` for clangd), and `mbt clean` / `distclean`. `-v`
on a command echoes the full cc370/as370/ld370/ar370 commands. `mbt` alone lists
every command.

**The startup banner is generated, and stays current on its own.** The version
and commit in `UFSD000I` / `UFSD001I` come from `.mbt/buildstamp.h`, which mbt
regenerates at every build (`MBT_VERSION`, `MBT_COMMIT`, `MBT_COMMIT_DIRTY`;
`ufsd.c` includes it as `<buildstamp.h>`). Because it is a header, `-MMD` makes
it a prerequisite of `ufsd.o`, so a version bump or a new commit recompiles that
one object — no `mbt clean` needed. And because mbt rewrites the file only when
a value actually changed, an unchanged commit recompiles nothing. `UFSD006W`
follows the same stamp: it appears when the build carried uncommitted *tracked*
changes.

### Project Structure

```
ufsd/
  include/           C headers (ufsd.h, libufs.h, ufsdrc.h)
  src/               Server source files (18 modules)
  client/            Client library and test programs
  samplib/           Sample JCL procedures and Parmlib member
  jcl/               Batch JCL
  docs/              User documentation (installation, configuration, disk setup, recovery, uninstall)
  internals/         Maintainer documentation (design concept, cross-AS reference, this guide, disk format spec, releasing)
  test/              Tests (every test/**/*.c is one, see Testing)
  tools/             Build-side checks (check-module-data.py)
  mbt.toml           Build configuration: version, toolchain, modules, tests, SMP package
```

### Installing libufs for Consumers

Programs that call libufs need the header at compile time and the `libufs.a`
archive at link time. Both ship together in one release artifact.

Declare the dependency in your `mbt.toml` (mbt 3) and run `mbt deps`, or in your
`project.toml` (mbt 2) and run `make deps` — the entry is the same:

```toml
[dependencies]
"mvslovers/ufsd" = ">=1.0.0-dev"
```

The dependency step then resolves the range against the ufsd GitHub Releases, downloads
`ufsd-<version>-lib.tar.gz`, and stages its `include/` + `lib/libufs.a` under
`.mbt/deps/ufsd/`. The build wires these in automatically: `-I .mbt/deps/ufsd/include`
on compile and `.mbt/deps/ufsd/lib/libufs.a` autocalled by `ld370` on link — no
MVS-side link library or `SYSLIB` concatenation is involved. It also
writes the resolved version + SHA256 to `mbt.lock` (commit it).

To fetch the artifact by hand instead, download `ufsd-<version>-lib.tar.gz` from
the [Releases](https://github.com/mvslovers/ufsd/releases) page and extract its
`include/*.h` and `lib/libufs.a` into your project.

## Testing

Every `test/**/*.c` is a test, named after its file in upper case
(`test/mvs/tstufsg.c` is `TSTUFSG`), one translation unit with a `main()` each.
A `[test.NAME]` entry in `mbt.toml` adds what differs from that: the source
under test, `host = false` for a test only the target can run. `LIBUFTST`
(`client/libufstst.c` + `client/libufs.c`) lives outside `test/` and is declared
there in full: an integration test that drives the libufs client API through a
running server.

Tests use `#include <mbtcheck.h>` and its `CHECK` / `CHECK_EQ` /
`mbt_test_summary` macros — portable C, so the same source runs both natively on
the host and on the MVS target. The only hard contract is the return code
(`0` = all passed), which becomes the job-step condition code on MVS.

```sh
mbt build --tests   # build the test load modules
mbt test            # build + run the tests natively on the host — fast inner loop
mbt test --mvs      # build + deploy the tests to a TESTLIB + run them on MVS
mbt check           # run every available suite (host first, then MVS)
```

`mbt test --mvs` deploys to a separate `…TESTLIB` and prints a per-test pass/fail
matrix. `LIBUFTST` needs a running UFSD: it drives the server through the
SSI. Run only selected tests with `mbt test --mvs --only LIBUFTST`.

## Architecture

### Cross-Address-Space Communication

UFSD runs as a Started Task in its own address space. Clients (HTTPD, FTPD, batch programs) communicate with it through a lock-free CSA queue and the MVS Subsystem Interface (SSI):

```
Client Address Space          UFSD STC Address Space
─────────────────────         ──────────────────────────────
  your program                  dispatch loop
    │                             ├─ session table (64 slots)
    ▼                             ├─ global file table (256)
  libufs (client stub)            ├─ inode / directory / block I/O
    │  IEFSSREQ (SVC 34)          └─ BDAM datasets (via DYNALLOC)
    ▼
  SSI router (UFSDSSIR)
    │  CS lock-free enqueue
    │  __xmpost to wake server
    │  WAIT on stack ECB
    └──── CSA ────────────────▶ UFSD_ANCHOR
                                  ├─ Request Pool (32 × ~296B)
                                  ├─ Buffer Pool (16 × 4K)
                                  └─ Trace Ring (256 × 16B)
```

No Cross-Memory Services (PC/PT) are available on S/370. The IPC uses IEFSSREQ (SVC 34) for routing, CS instructions for lock-free queuing, `__xmpost` (CVT0PT01) for cross-AS POST, and WAIT (SVC 1) on a key-8 stack ECB.

### Source Modules

| Module | File | Lines | Description |
|--------|------|-------|-------------|
| UFSD | `src/ufsd.c` | 444 | STC main, dispatch loop, ESTAE, shutdown |
| UFSD#CMD | `src/ufsd#cmd.c` | 357 | MODIFY command parser |
| UFSD#CFG | `src/ufsd#cfg.c` | 245 | Parmlib parser |
| UFSD#CSA | `src/ufsd#csa.c` | 225 | CSA allocation, request pool |
| UFSD#BUF | `src/ufsd#buf.c` | 78 | 4K buffer pool (CS lock-free) |
| UFSD#SCT | `src/ufsd#sct.c` | 159 | SSCT/SSVT registration |
| UFSD#SSI | `src/ufsd#ssi.c` | 436 | SSI thin router (runs in client AS) |
| UFSD#QUE | `src/ufsd#que.c` | 274 | Lock-free queue, dispatch routing |
| UFSD#SES | `src/ufsd#ses.c` | 434 | Session management |
| UFSD#INI | `src/ufsd#ini.c` | 747 | Disk init (DYNALLOC, BDAM open, mount) |
| UFSD#BLK | `src/ufsd#blk.c` | 80 | BDAM block read/write |
| UFSD#SBL | `src/ufsd#sbl.c` | 486 | Superblock, alloc, chain/inode/bitmap refill |
| UFSD#INO | `src/ufsd#ino.c` | 85 | Inode I/O |
| UFSD#DIR | `src/ufsd#dir.c` | 326 | Directory lookup/add/remove |
| UFSD#FIL | `src/ufsd#fil.c` | 1464 | File operation dispatch |
| UFSD#GFT | `src/ufsd#gft.c` | 119 | Global File Table |
| UFSD#TRC | `src/ufsd#trc.c` | 109 | Trace ring buffer |
| UFSD#RCL | `src/ufsd#rcl.c` | 274 | Orphan reclaim + liveness guard (shared by startup and UFSDCLNP) |
| UFSD#ASV | `src/ufsd#asv.c` | 36 | ASVT membership scan (portable, host-tested) |
| UFSDCLNP | `src/ufsdclnp.c` | 70 | Emergency cleanup (no-IPL recovery) |
| LIBUFS | `client/libufs.c` | 1010 | Client stub library |
| LIBUFTST | `client/libufstst.c` | 345 | Integration test client |
| **Total** | | **~7600** | Server + client |

### On-Disk Format

The UFS370 on-disk format is documented in [internals/ufsdisk-spec.md](ufsdisk-spec.md). Key characteristics: block sizes 512–8192 (default 4096), 128-byte inodes with dual-format timestamps, 64-byte directory entries (59-char filenames), V7 free block chain with inode scan refill and bitmap fallback. Single indirect blocks support files up to 4.06 MB.

## libufs API Reference

Include `libufs.h` and link against the `libufs.a` archive (staged by `mbt deps`, autocalled by `ld370`).

This section is a short orientation for maintainers. What the calls do, what
they return and where they differ from `stdio` is in the _UFSD User's Guide_
(ML05-0001), chapter 6, "Programming with libufs", and in the _UFSD Reference_
(ML05-0002), chapter 5, "The Client Library libufs". Where this section and the
books disagree, `include/libufs.h` and `client/libufs.c` decide, and the
books were checked against them.

```c
#include "libufs.h"
```

### Session Lifecycle

Every program that uses UFSD opens a session with `ufsnew()` and releases it with `ufsfree()`.

```c
UFS *ufs;

ufs = ufsnew();
if (ufs == NULL) {
    /* UFSD not running, or all 64 session slots are occupied */
    return 1;
}

/* ... do work ... */

ufsfree(&ufs);   /* closes session, ufs is set to NULL */
```

### Session User Identity

Set the session owner for per-user permission checks on OWNER-restricted mounts. Typically called after `ufsnew()`:

```c
ufs_setuser(ufs, "IBMUSER", "SYS1");
```

### File I/O

`ufs_fopen` looks at the first character of the mode only. `r` or `R` opens for reading (the file must exist); every other mode opens for writing and creates the file or empties it (`UFSD_OPEN_WRITE`, "create + truncate", `src/ufsd#fil.c`). `"a"` does not append, and `"r+"` / `"w+"` do not give a handle for both directions. UFSD filesystems are EBCDIC; text and binary mode have the same effect on MVS. The other differences from `stdio` are listed in the User's Guide, "Where libufs Differs from stdio".

```c
UFSFILE *fp;
char     buf[256];
UINT32   n;

/* Open for writing */
fp = ufs_fopen(ufs, "/u/ibmuser/hello.txt", "w");
if (fp == NULL) {
    return 1;
}

ufs_fputs("Hello, World!\n", fp);
ufs_fclose(&fp);

/* Open for reading */
fp = ufs_fopen(ufs, "/u/ibmuser/hello.txt", "r");
if (fp == NULL) {
    return 1;
}

while (ufs_fgets(buf, sizeof(buf), fp) != NULL) {
    /* process line in buf */
}

ufs_fclose(&fp);
```

**Block I/O** — use `ufs_fread` / `ufs_fwrite` for binary data:

```c
UFSFILE *fp;
char     image[4096];
UINT32   nread;

fp = ufs_fopen(ufs, "/u/ibmuser/data.bin", "rb");
if (fp == NULL) {
    return 1;
}

nread = ufs_fread(image, 1, sizeof(image), fp);
/* nread = items read (bytes here, as size is 1); fewer than asked for
   means end of file or an error: ufs_feof(fp), ufs_ferror(fp) */

ufs_fclose(&fp);
```

### Directory Listing

```c
UFSDDESC *ddesc;
UFSDLIST *entry;

/* List all entries under /u/ibmuser. The pattern is ignored (see below). */
ddesc = ufs_diropen(ufs, "/u/ibmuser", NULL);
if (ddesc == NULL) {
    return 1;
}

while ((entry = ufs_dirread(ddesc)) != NULL) {
    /*
     * entry->name        — file name (NUL-terminated)
     * entry->filesize    — size in bytes
     * entry->attr        — mode string, e.g. "-rwxrwxrwx"
     * entry->owner       — RACF userid
     * entry->nlink       — link count
     * entry->mtime       — modification time (mtime64_t, ms since epoch)
     */
}

ufs_dirclose(&ddesc);
```

The third argument of `ufs_diropen` (the pattern) is ignored whatever its value (`(void)pattern;` in `client/libufs.c`): every entry is returned, including `.` and `..`. A caller that wants only `*.html` filters on `entry->name` itself.

### Directory and File Operations

```c
/* Change working directory */
ufs_chgdir(ufs, "/u/ibmuser");

/* Create a directory */
ufs_mkdir(ufs, "/u/ibmuser/docs");

/* Remove an empty directory */
ufs_rmdir(ufs, "/u/ibmuser/tmp");

/* Delete a file */
ufs_remove(ufs, "/u/ibmuser/old.txt");

/* Stat a file or directory (metadata without open) */
UFSDLIST info;
if (ufs_stat(ufs, "/u/ibmuser/index.html", &info) == 0) {
    /* info.filesize, info.attr, info.owner, info.mtime, ... */
}
```

### Seek and Sync

`ufs_fseek` is not implemented: it always returns -1 and does nothing, and there is no `ufs_ftell`. To read a file again, close it and open it again. `ufs_sync` does nothing. `ufs_fsync(fp)` sends the text that `ufs_fputs` / `ufs_fputc` have collected in the handle; call it, and look at `ufs_ferror`, before `ufs_fclose`, which ignores a failure of the last send.

```c
ufs_fsync(fp);
if (ufs_ferror(fp)) {
    /* the collected text did not reach the file */
}
```

### Error Handling

`ufs_fopen` and `ufs_diropen` return `NULL` on failure, the directory and path functions a value other than 0, and `ufsnew` returns `NULL` with no code. `ufs_last_rc()` returns the UFSD return code after `ufs_fopen` and the directory and path functions (a call refused before it is sent leaves it unchanged). It is not set reliably after `ufs_diropen` and not at all by `ufsnew` and by the calls on a file handle; the User's Guide, "Handling Errors", lists case by case. Example after a directory call:

```c
if (ufs_mkdir(ufs, "/u/ibmuser/docs") != 0) {
    int rc = ufs_last_rc(ufs);
    /* rc is one of the UFSD_RC_* codes from ufsdrc.h */
}
```

For file I/O, use `ufs_ferror()` on the file handle after a short read or write:

```c
UINT32 n;

n = ufs_fwrite(data, 1, datalen, fp);
if (n < datalen) {
    if (ufs_ferror(fp)) {
        /* I/O error or filesystem full */
    }
}

ufs_clearerr(fp);
```

### Return Codes (ufsdrc.h)

| Code | Name | Description |
|------|------|-------------|
| 0 | `UFSD_RC_OK` | Success |
| 4 | `UFSD_RC_NOREQ` | No free request block available |
| 8 | `UFSD_RC_CORRUPT` | Corrupt request block or server not responding |
| 12 | `UFSD_RC_BADFUNC` | Invalid function code |
| 16 | `UFSD_RC_BADSESS` | Invalid session token |
| 20 | `UFSD_RC_INVALID` | Validation failed |
| 24 | `UFSD_RC_NOTIMPL` | Not yet implemented |
| 28 | `UFSD_RC_NOFILE` | File or path not found |
| 32 | `UFSD_RC_EXIST` | File/directory already exists |
| 36 | `UFSD_RC_NOTDIR` | Not a directory |
| 40 | `UFSD_RC_ISDIR` | Is a directory (not a file) |
| 44 | `UFSD_RC_NOSPACE` | No free disk blocks, **or** the per-file size ceiling was reached |
| 48 | `UFSD_RC_NOINODES` | No free inodes |
| 52 | `UFSD_RC_IO` | I/O error |
| 56 | `UFSD_RC_BADFD` | Bad file descriptor |
| 60 | `UFSD_RC_NOTEMPTY` | Directory not empty |
| 64 | `UFSD_RC_NAMETOOLONG` | Filename exceeds 59 characters |
| 68 | `UFSD_RC_ROFS` | Read-only filesystem |
| 72 | `UFSD_RC_EACCES` | Permission denied (owner check) |

**Rc 44 is not only "disk full".** `blk_alloc_at()` returns `UFSD_RC_NOSPACE`
just as readily when a write crosses the single-indirect ceiling:
`UFSD_NADDR_DIRECT + blksize/4` blocks, which is 1040 blocks or **4.06 MB** at
the usual `BLKSIZE=4096`. Double and triple indirect are reserved by the on-disk
format and never followed, so a file larger than that fails mid-write on a
filesystem with any amount of free space. A client mapping rc 44 to a protocol
reply should not word it as "filesystem full". See
[ufsdisk-spec.md](ufsdisk-spec.md) §5.5 for the capacity table.

**Rc 48 never comes from a write.** `UFSD_RC_NOINODES` is raised only where an
inode is allocated -- `ufs_fopen()` on a create and `ufs_mkdir()` -- so it is
reported through `ufs_last_rc()` after one of those, never through
`ufs_ferror()` after `ufs_fwrite()`.

### Complete Example

```c
/*
 * Copy a local MVS dataset member into a UFSD filesystem.
 * Build: declare "mvslovers/ufsd" under [dependencies] in mbt.toml, run
 * `mbt deps`, then `mbt build` -- ld370 autocalls libufs.a from .mbt/deps.
 */
#include <stdio.h>
#include <string.h>
#include "libufs.h"

int main(void)
{
    UFS     *ufs;
    UFSFILE *dst;
    FILE    *src;
    char     buf[4096];
    size_t   n;

    ufs = ufsnew();
    if (ufs == NULL) {
        fprintf(stderr, "ufsnew: UFSD not available\n");
        return 1;
    }

    src = fopen("INPUT", "rb");         /* MVS DD:INPUT */
    if (src == NULL) {
        fprintf(stderr, "fopen: INPUT failed\n");
        ufsfree(&ufs);
        return 1;
    }

    dst = ufs_fopen(ufs, "/u/ibmuser/upload.bin", "wb");
    if (dst == NULL) {
        fprintf(stderr, "ufs_fopen: failed\n");
        fclose(src);
        ufsfree(&ufs);
        return 1;
    }

    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (ufs_fwrite(buf, 1, (UINT32)n, dst) < n) {
            fprintf(stderr, "ufs_fwrite: short write\n");
            break;
        }
    }

    ufs_fclose(&dst);
    fclose(src);
    ufsfree(&ufs);
    return 0;
}
```

## Using libufs from Assembler

The calling convention is described once, in the _UFSD User's Guide_
(ML05-0001), chapter 6, section "Using libufs from Assembler", with programs
that were built and run (`docs/books/ex/ug-libufs/`: `asmput.asm`,
`ufsasmb.asm`, `ufsasmd.asm`, and `ufsasmc.asm`, which shows the failure). The
Reference lists the entry name of every function. This guide keeps no copy.

Two points decide whether a call works at all, and are easy to get wrong:

- The functions need the C environment (C stack and heap). A direct call from
  plain OS linkage, with a save area and no C environment, ended in S0C1 in the measurement. Call
  from a C main program, through a `CCALL` stub, or under the C startup.
- The parameter list that R1 points to holds the argument *values* (pointers
  or integers), not the addresses of fullwords that hold them, with the high
  bit set in the last word.
