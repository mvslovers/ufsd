# UFSD — Development Guide

This document covers building UFSD from source, the server architecture, the complete libufs API reference, and assembler calling conventions.

## Building from Source

### Prerequisites

- Linux or macOS build host
- The `cc370` cross-toolchain (`cc370`, `as370`, `ld370`, `ar370`) on `PATH`, GNU Make, Python 3.12+
- MVS 3.8j system running the [mvsMF](https://github.com/mvslovers/mvsmf) REST API (port 1080 by default) — only needed for `make deploy` and `make test-mvs`; the build itself runs entirely on the host
- The `mbt` submodule is included in the repository

### Build Commands

```sh
git clone --recurse-submodules https://github.com/mvslovers/ufsd
cd ufsd
cp .env.example .env
```

Edit `.env` and set at minimum:

```sh
MBT_MVS_HOST=192.168.1.x       # IP of your Hercules system
MBT_MVS_PORT=1080               # mvsMF port
MBT_MVS_USER=IBMUSER
MBT_MVS_PASS=SYS1
MBT_MVS_HLQ=IBMUSER             # high-level qualifier for build datasets
MBT_MVS_DEPS_HLQ=IBMUSER.DEPS   # HLQ for dependency datasets
```

Then build:

```sh
make deps         # download + stage declared dependencies (none for ufsd today)
make              # build the load modules on the host (default target)
make lib          # build the libufs static archive (build/libufs.a)
make package      # produce distribution archives in dist/
make deploy       # XMIT + upload the load modules into the MVS LINKLIB
```

The entire build runs on the host with the `cc370` toolchain (`cc370` → `.o`,
`as370`, `ld370`, `ar370`); MVS is touched only by `make deploy`. The load
modules are written locally under `build/` (e.g. `build/UFSD.iebcopy`) and reach
the MVS `LINKLIB` only after `make deploy`. The build is driven by `project.toml`
(project metadata, modules, dependencies). `libc370` is the cc370 sysroot
(`-lc`), not a declared dependency; `ufsd` itself currently declares no
`[dependencies]`, so `make deps` is a no-op until one is added.

Other targets: `make all` (modules + library), `make modules`, the test targets
(`make test` / `test-host` / `test-mvs` / `check`, see [Testing](#testing)),
`make doctor` (check the toolchain + MVS connectivity), `make compiledb` (write
`compile_commands.json` for clangd), and `make clean` / `distclean`.
`VERBOSE=1 make` echoes the full cc370/as370/ld370/ar370 commands.

**Run `make clean` after a version bump.** The version in the startup banner
(`UFSD000I` / `UFSD001I`) comes from `-DVERSION`, which `project.toml` puts on
the compile line from `VERSION`. Changing `VERSION` invalidates no object file,
so an incremental build keeps whatever string `ufsd.o` was compiled with and the
server reports the old version — while every other module in the load library is
current. A deploy can therefore be entirely correct and still be contradicted by
the banner. Do not use the banner alone to confirm which build is running; a
string that only exists in the new code is the reliable check.

### Project Structure

```
ufsd/
  include/           C headers (ufsd.h, libufs.h, ufsdrc.h)
  src/               Server source files (18 modules)
  client/            Client library and test programs
  samplib/           Sample JCL procedures and Parmlib member
  jcl/               Batch JCL
  docs/              Documentation and design docs (concept, disk spec)
  mbt/               MVS Build Tools submodule (cc370 build system)
  project.toml       Build configuration, modules, dependencies
  Makefile           Two-line include of mbt/mk/mbt.mk
  VERSION            Version string for release automation
  mbt.lock           Resolved dependency pins (committed; empty when no deps)
```

### Installing libufs for Consumers

Programs that call libufs need the header at compile time and the `libufs.a`
archive at link time. Both ship together in one release artifact.

The mbt v2 way — declare the dependency in your `project.toml`:

```toml
[dependencies]
"mvslovers/ufsd" = ">=1.0.0-dev"
```

`make deps` then resolves the range against the ufsd GitHub Releases, downloads
`ufsd-<version>-lib.tar.gz`, and stages its `include/` + `lib/libufs.a` under
`.mbt/deps/ufsd/`. The build wires these in automatically: `-I .mbt/deps/ufsd/include`
on compile and `.mbt/deps/ufsd/lib/libufs.a` autocalled by `ld370` on link — no
MVS-side link library or `SYSLIB` concatenation is involved. `make deps` also
writes the resolved version + SHA256 to `mbt.lock` (commit it).

To fetch the artifact by hand instead, download `ufsd-<version>-lib.tar.gz` from
the [Releases](https://github.com/mvslovers/ufsd/releases) page and extract its
`include/*.h` and `lib/libufs.a` into your project.

## Testing

Tests are declared as `[[test]]` blocks in `project.toml` (one translation unit
with a `main()` each). UFSD ships one: `LIBUFTST` (`client/libufstst.c` +
`client/libufs.c`), an integration test that exercises the libufs client API.

Tests use `#include <mbtcheck.h>` and its `CHECK` / `CHECK_EQ` /
`mbt_test_summary` macros — portable C, so the same source runs both natively on
the host and on the MVS target. The only hard contract is the return code
(`0` = all passed), which becomes the job-step condition code on MVS.

```sh
make test        # build the test load modules
make test-host   # build + run the tests natively on the host — fast inner loop
make test-mvs    # build + deploy the tests to a TESTLIB + run them on MVS
make check       # run every available suite (host first, then MVS)
```

`make test-mvs` deploys to a separate `…TESTLIB` and prints a per-test pass/fail
matrix. The production `LINKLIB` must already be deployed, since the MVS tests
`LOAD` the server modules from it. Run only selected tests with
`make test-mvs ARGS="--only LIBUFTST"`.

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
| UFSDCLNP | `src/ufsdclnp.c` | 198 | Emergency cleanup (no-IPL recovery) |
| LIBUFS | `client/libufs.c` | 1010 | Client stub library |
| LIBUFTST | `client/libufstst.c` | 345 | Integration test client |
| **Total** | | **~7600** | Server + client |

### On-Disk Format

The UFS370 on-disk format is documented in [docs/ufsdisk-spec.md](ufsdisk-spec.md). Key characteristics: block sizes 512–8192 (default 4096), 128-byte inodes with dual-format timestamps, 64-byte directory entries (59-char filenames), V7 free block chain with inode scan refill and bitmap fallback. Single indirect blocks support files up to 4.06 MB.

## libufs API Reference

Include `libufs.h` and link against the `libufs.a` archive (staged by `make deps`, autocalled by `ld370`).

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

Open modes follow `fopen(3)` conventions: `"r"`, `"w"`, `"a"`, `"rb"`, `"wb"`. UFSD filesystems are EBCDIC; text and binary mode have the same effect on MVS.

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
/* nread = bytes actually read; ufs_feof(fp) is set after the last block */

ufs_fclose(&fp);
```

### Directory Listing

```c
UFSDDESC *ddesc;
UFSDLIST *entry;

/* List all entries under /u/ibmuser matching "*.html" */
ddesc = ufs_diropen(ufs, "/u/ibmuser", "*.html");
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

Use `NULL` as the pattern to list all entries:

```c
ddesc = ufs_diropen(ufs, "/u/ibmuser", NULL);
```

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

```c
/* Seek within an open file */
ufs_fseek(fp, 0, UFS_SEEK_SET);    /* rewind to beginning */
ufs_fseek(fp, 100, UFS_SEEK_CUR);  /* skip 100 bytes forward */
ufs_fseek(fp, -10, UFS_SEEK_END);  /* 10 bytes before end */

/* Flush pending writes to the server */
ufs_fsync(fp);      /* flush a single file handle */
ufs_sync(ufs);      /* flush all open files in the session */
```

### Error Handling

File functions return `NULL` on failure. Integer functions return a non-zero value on error. Use `ufs_last_rc()` to retrieve the UFSD return code after a failed operation:

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
| 44 | `UFSD_RC_NOSPACE` | No free disk blocks |
| 48 | `UFSD_RC_NOINODES` | No free inodes |
| 52 | `UFSD_RC_IO` | I/O error |
| 56 | `UFSD_RC_BADFD` | Bad file descriptor |
| 60 | `UFSD_RC_NOTEMPTY` | Directory not empty |
| 64 | `UFSD_RC_NAMETOOLONG` | Filename exceeds 59 characters |
| 68 | `UFSD_RC_ROFS` | Read-only filesystem |
| 72 | `UFSD_RC_EACCES` | Permission denied (owner check) |

### Complete Example

```c
/*
 * Copy a local MVS dataset member into a UFSD filesystem.
 * Build via mbt v2: declare "mvslovers/ufsd" in project.toml, run
 * `make deps`, then `make` -- ld370 autocalls libufs.a from .mbt/deps.
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

libufs functions follow the standard IBM OS/VS C linkage convention:

- Register 1 points to a **parameter list** — a list of fullword addresses, one per argument.
- Each address in the parameter list points to a fullword containing the actual argument value (pointer or integer).
- The high bit of the last address in the list is set (variable-length plist indicator).
- The return value (pointer or integer) is in Register 15.
- Register 13 must point to an 18-word (72-byte) save area on entry.
- Registers 14, 15, 0, and 1 are not preserved across the call.

### Example: Open a Session and Read a File

```asm
*----------------------------------------------------------------*
* UFSDEMO -- read /u/ibmuser/index.html using libufs                   *
*----------------------------------------------------------------*
UFSDEMO  CSECT
UFSDEMO  AMODE 24
UFSDEMO  RMODE 24
         USING UFSDEMO,R12
         STM   R14,R12,12(R13)    save caller registers
         LR    R12,R15            establish base
         LA    R15,SAVEAREA
         ST    R13,4(R15)         back-chain
         ST    R15,8(R13)         forward-chain
         LR    R13,R15
*
*-- ufsnew() --------------------------------------------------*
*   UFS *ufsnew(void)  -- no parameters
         SR    R1,R1              R1 = 0 (no plist)
         L     R15,=V(UFSNEW)
         BALR  R14,R15
         LTR   R15,R15            NULL = UFSD not available
         BZ    ERROUT
         ST    R15,UFSHDL         save UFS* handle
*
*-- ufs_fopen(ufs, path, mode) --------------------------------*
*   UFSFILE *ufs_fopen(UFS *ufs, const char *path, const char *mode)
         L     R2,UFSHDL
         ST    R2,P_UFS           plist slot: UFS* value
         LA    R2,FILEPATH
         ST    R2,P_PATH          plist slot: path ptr value
         LA    R2,OPENMODE
         ST    R2,P_MODE          plist slot: mode ptr value
         LA    R1,PL_FOPEN        R1 -> parameter list
         L     R15,=V(UFS#FOPN)
         BALR  R14,R15
         LTR   R15,R15            NULL = open failed
         BZ    ERROUT
         ST    R15,FILHDL         save UFSFILE* handle
*
*-- ufs_fread(buf, size, nitems, fp) --------------------------*
*   UINT32 ufs_fread(void *ptr, UINT32 size, UINT32 nitems, UFSFILE *fp)
         LA    R2,READBUF
         ST    R2,P_BUF           plist slot: buffer ptr value
         LA    R2,=F'1'
         ST    R2,P_SZ            plist slot: element size (1)
         LA    R2,=F'256'
         ST    R2,P_CNT           plist slot: element count
         L     R2,FILHDL
         ST    R2,P_FP            plist slot: UFSFILE* value
         LA    R1,PL_FREAD
         L     R15,=V(UFS#FRD)
         BALR  R14,R15
         ST    R15,NREAD          bytes read returned in R15
*
*-- ufs_fclose(&filhdl) ---------------------------------------*
*   void ufs_fclose(UFSFILE **file)
         LA    R2,FILHDL          address of UFSFILE* variable
         ST    R2,P_FPP           plist slot: UFSFILE** value
         LA    R1,PL_FCLOSE
         L     R15,=V(UFS#FCLS)
         BALR  R14,R15
*
*-- ufsfree(&ufshdl) ------------------------------------------*
*   void ufsfree(UFS **ufs)
         LA    R2,UFSHDL          address of UFS* variable
         ST    R2,P_UFSPP         plist slot: UFS** value
         LA    R1,PL_FFREE
         L     R15,=V(UFSFREE)
         BALR  R14,R15
*
DONE     SR    R15,R15
         B     RETURN
ERROUT   LA    R15,8
RETURN   L     R13,4(R13)
         L     R14,12(R13)
         LM    R0,R12,20(R13)
         BR    R14
*
*----------------------------------------------------------------*
* Parameter list storage (one fullword per argument value)        *
*----------------------------------------------------------------*
P_UFS    DS    A              UFS* value for ufs_fopen
P_PATH   DS    A              path ptr value
P_MODE   DS    A              mode ptr value
P_BUF    DS    A              buffer ptr value for ufs_fread
P_SZ     DS    A              element size
P_CNT    DS    A              element count
P_FP     DS    A              UFSFILE* value for ufs_fread
P_FPP    DS    A              UFSFILE** value for ufs_fclose
P_UFSPP  DS    A              UFS** value for ufsfree
*
*-- Parameter lists (address of argument storage, VL marker) ----*
PL_FOPEN DC    A(P_UFS)       -> UFS*
         DC    A(P_PATH)      -> path ptr
         DC    X'80',AL3(P_MODE)  -> mode ptr  [VL]
*
PL_FREAD DC    A(P_BUF)       -> buffer ptr
         DC    A(P_SZ)        -> size
         DC    A(P_CNT)       -> nitems
         DC    X'80',AL3(P_FP)   -> UFSFILE*  [VL]
*
PL_FCLOSE DC   X'80',AL3(P_FPP) -> UFSFILE** [VL]
*
PL_FFREE  DC   X'80',AL3(P_UFSPP) -> UFS**   [VL]
*
*-- Working storage ---------------------------------------------*
UFSHDL   DS    A              UFS session handle
FILHDL   DS    A              UFSFILE handle
NREAD    DS    F              bytes read
FILEPATH DC    C'/u/ibmuser/index.html',X'00'
OPENMODE DC    C'r',X'00'
READBUF  DS    CL256
SAVEAREA DS    18F
         END   UFSDEMO
```

### Notes for Assembler Callers

**External entry-point names use the 8-character MVS convention, not the C names.** `ufs_fopen` links as `UFS#FOPN`, `ufs_fread` as `UFS#FRD`, `ufs_fclose` as `UFS#FCLS`, and so on — consult the `asm("…")` labels in `libufs.h` for the exact entry name of each function. `ufsnew` and `ufsfree` keep their C names (`UFSNEW`, `UFSFREE`). The `#` is a valid national character in `as370` symbols.

**`ufsnew` has no parameters.** Set R1 to zero or leave it undefined; the function does not dereference R1.

**`ufsfree` and `ufs_fclose` take a pointer-to-pointer** (`UFS **` and `UFSFILE **`). Pass the address of your handle variable, not the handle value itself. After the call the handle is set to NULL.

**`ufs_fread` and `ufs_fwrite` return the number of items** (not bytes) when `size > 1`; with `size = 1` the return value is the byte count. The return is in R15 as a fullword unsigned integer.

**String arguments** (path, mode) must be NUL-terminated EBCDIC strings. Use `DC C'...',X'00'` in your assembler source.

**The 4 KB buffer path** is used transparently when `ufs_fread` / `ufs_fwrite` transfers more than 248 bytes in a single call. No special action is required from the caller; the SSI router handles the copy through the CSA buffer pool.
