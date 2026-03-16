# UFSD — Virtual Filesystem Daemon for MVS 3.8j

UFSD is a cross-address-space virtual filesystem server for MVS 3.8j. It runs
as a Started Task (STC) and gives client programs Unix-like filesystem access
to BDAM datasets. Communication happens via IEFSSREQ (SVC 34); the `libufs`
client library provides a clean C API and serves as a drop-in replacement for
ufs370.

```
Client Address Space          UFSD STC Address Space
─────────────────────         ──────────────────────────────
  your program                  dispatch loop
    │                             ├─ session table (64 slots)
    ▼                             ├─ global file table (256)
  libufs                          ├─ inode / directory / block I/O
    │  IEFSSREQ (SVC 34)          └─ BDAM datasets
    ▼
  SSI router (UFSDSSIR)
    │  CS lock-free queue
    └──── CSA ────────────────▶ UFSD_ANCHOR (request + buffer pools)
```

---

## Contents

- [Installation](#installation)
  - [From a GitHub Release](#from-a-github-release)
  - [Building from Source](#building-from-source)
  - [Installing libufs for Consumers](#installing-libufs-for-consumers)
- [System Configuration](#system-configuration)
  - [APF Authorization](#1-apf-authorization)
  - [JCL Procedure](#2-jcl-procedure)
  - [Disk Datasets](#3-disk-datasets)
  - [Started Task User](#4-started-task-user)
- [Operating UFSD](#operating-ufsd)
  - [Starting and Stopping](#starting-and-stopping)
  - [Operator Commands](#operator-commands)
  - [Mounting Filesystems](#mounting-filesystems)
- [Using libufs in C Programs](#using-libufs-in-c-programs)
  - [Session Lifecycle](#session-lifecycle)
  - [File I/O](#file-io)
  - [Directory Listing](#directory-listing)
  - [Error Handling](#error-handling)
  - [Complete Example](#complete-example)
- [Using libufs from Assembler](#using-libufs-from-assembler)

---

## Installation

### From a GitHub Release

**Prerequisites:** Hercules with TK4-, TK5, or MVS/CE; FTP or zowe CLI access.

1. Download `ufsd-<version>-mvs.tar.gz` from the [Releases] page and extract it.
   You will find one or more `.XMI` files (XMIT archives).

2. Upload the LOAD library archive to MVS as a binary sequential dataset:

   ```sh
   # zowe CLI
   zowe files upload file-to-data-set UFSD.LOAD.XMI "IBMUSER.UFSD.XMIT" --binary

   # or via FTP (binary mode)
   ftp mvs-host
   binary
   put UFSD.LOAD.XMI 'IBMUSER.UFSD.XMIT'
   ```

3. On TSO, restore the load library:

   ```
   RECEIVE INDSNAME('IBMUSER.UFSD.XMIT')
   ```

   When prompted for restore parameters, specify the target dataset name:

   ```
   DSNAME(IBMUSER.UFSD.LOAD)
   ```

4. The restored PDS contains:

   | Member    | Description                            |
   |-----------|----------------------------------------|
   | UFSD      | Main STC load module                   |
   | UFSDSSIR  | SSI thin router (loaded into CSA)      |
   | UFSDCLNP  | Emergency cleanup utility              |
   | UFSDTEST  | Session lifecycle test client          |
   | UFSDPING  | Ping/pong connectivity test            |
   | LIBUFTST  | libufs integration test client         |

   Copy the members you need into an existing system load library, or use
   `IBMUSER.UFSD.LOAD` directly as a `STEPLIB`.

[Releases]: https://github.com/mvslovers/ufsd/releases

---

### Building from Source

**Prerequisites:**

- Linux or macOS build host
- GCC, GNU Make, Python 3.8+
- MVS 3.8j system running the mvsMF REST API (port 1080 by default)
- The `mbt` submodule is included in the repository

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
MBT_MVS_DEPS_HLQ=MBTDEPS        # HLQ for dependency datasets
MBT_MVS_DEPS_VOLUME=PUB000      # volume for TSO RECEIVE (MVS/CE: required)
```

Then build:

```sh
make bootstrap    # fetch crent370 headers + NCALIB from GitHub
make build        # cross-compile C → S/370 ASM, assemble on MVS
make link         # linkedit load modules on MVS
```

The finished load modules land in `{HLQ}.UFSD.V1R0M0D.LOAD` on MVS.

To package for distribution:

```sh
make package      # TRANSMIT load library → downloads UFSD.LOAD.XMI
```

---

### Installing libufs for Consumers

Programs that call libufs at compile time need the header; at link time they
need the NCALIB member.

1. Download `ufsd-<version>-headers.tar.gz` from the [Releases] page.
   Extract `libufs.h` into your project's `include/` (or `contrib/`).

2. Download `ufsd-<version>-mvs.tar.gz`, upload, and `RECEIVE` it to obtain
   the NCALIB dataset. Add it to your link JCL `SYSLIB` concatenation.

   With mbt, declare the dependency in your `project.toml`:

   ```toml
   [dependencies]
   "mvslovers/ufsd" = ">=1.0.0"
   ```

   `make bootstrap` then fetches and installs everything automatically.

---

## System Configuration

### 1. APF Authorization

UFSD allocates CSA storage (subpool 241, key 0) and operates in supervisor
state. Its load library must be APF-authorized.

Add to `SYS1.PARMLIB(IEAAPFxx)`:

```
APF ADD DSNAME(IBMUSER.UFSD.LOAD) VOLUME(volser)
```

Or authorize it dynamically without an IPL:

```
SETPROG APF,ADD,DSNAME=IBMUSER.UFSD.LOAD,VOLUME=volser
```

Verify with:

```
D PROG,APF,DSNAME=IBMUSER.UFSD.LOAD
```

---

### 2. JCL Procedure

Add member `UFSD` to `SYS1.PROCLIB` or `SYS2.PROCLIB`:

```jcl
//UFSD     PROC
//UFSD     EXEC PGM=UFSD
//STEPLIB  DD   DSN=IBMUSER.UFSD.LOAD,DISP=SHR
//SYSPRINT DD   SYSOUT=*
//SYSUDUMP DD   SYSOUT=*
```

`STEPLIB` must be APF-authorized (see step 1). If UFSD's load library is
already in the LNKLST, the `STEPLIB` DD is not required.

If you want to pre-define disk datasets so they can be mounted via the
`MOUNT DD=` operator command, add them here:

```jcl
//UFSD     PROC
//UFSD     EXEC PGM=UFSD
//STEPLIB  DD   DSN=IBMUSER.UFSD.LOAD,DISP=SHR
//SYSPRINT DD   SYSOUT=*
//SYSUDUMP DD   SYSOUT=*
//*
//* Pre-defined filesystem datasets (mount with: F UFSD,MOUNT DD=ddname,...)
//WEBROOT  DD   DSN=HTTPD.WEBROOT,DISP=SHR,
//              DCB=(RECFM=U,DSORG=DA,BLKSIZE=4096)
//USRHOME  DD   DSN=SYS1.UFSHOME,DISP=SHR,
//              DCB=(RECFM=U,DSORG=DA,BLKSIZE=4096)
```

---

### 3. Disk Datasets

Each UFSD filesystem lives in a BDAM dataset. Allocate one before use:

```jcl
//ALLOC   JOB  (A),'ALLOC',CLASS=A,MSGCLASS=H
//STEP1   EXEC PGM=IEFBR14
//WEBROOT DD   DSN=HTTPD.WEBROOT,
//             DISP=(NEW,CATLG),
//             UNIT=SYSDA,VOL=SER=PUB000,
//             SPACE=(TRK,(200,50)),
//             DCB=(RECFM=U,DSORG=DA,BLKSIZE=4096)
```

Choose `BLKSIZE` between 512 and 8192; 4096 is the recommended default.
The dataset must be DSORG=DA (direct access / BDAM).

To pre-format a new filesystem image, use `ufsd-utils` (a companion host-side
tool) or issue a `REBUILD` command after first mount (see below).

---

### 4. Started Task User

UFSD needs OPER authority (or equivalent) to allocate CSA storage and to
register the subsystem in the SSCT chain.

On TK4-/TK5, `IBMUSER` already has sufficient authority. For a dedicated STC
user on RAKF-protected systems, grant the `OPER` attribute or add the userid
to a suitable group.

To let MVS start the STC without a password, add the UFSD proc to the trusted
STC list. On TK4-/TK5, add to `SYS1.PARMLIB(IKJTSO00)`:

```
PROC(UFSD)
```

---

## Operating UFSD

### Starting and Stopping

Start the daemon:

```
S UFSD
```

UFSD registers itself as a subsystem named `UFSD` in the SSCT chain. Startup
messages appear on the operator console:

```
UFSD0001I UFSD STARTING
UFSD0002I CSA ANCHOR ALLOCATED AT xxxxxxxx
UFSD0003I SUBSYSTEM UFSD REGISTERED
UFSD0004I UFSD READY
```

Normal shutdown (drains pending requests, deregisters subsystem, frees CSA):

```
P UFSD
```

or:

```
F UFSD,SHUTDOWN
```

If UFSD abends and leaves its subsystem entry registered, subsequent starts
will fail. Run the cleanup utility to deregister the stale entry and free CSA:

```
S UFSDCLNP
```

---

### Operator Commands

All commands are issued with the MVS MODIFY command:

```
F UFSD,command
```

| Command | Description |
|---------|-------------|
| `STATS` | Print request counters, error count, POST bundles saved, free pool sizes, and a list of mounted filesystems |
| `SESSIONS` | List all active client sessions (userid, ASID, session token) |
| `MOUNT DD=ddname,PATH=/path` | Mount a pre-defined DD (from the PROC) at the given path |
| `MOUNT DSN=dsn,PATH=/path` | Mount a dataset by name (DYNALLOC; requires AP-3a) |
| `MOUNT ...,MODE=RO` | Mount read-only (write requests return ROFS) |
| `MOUNT ...,MODE=RW` | Mount read-write (default) |
| `MOUNT ...,OWNER=userid` | Restrict write access to the named userid |
| `UNMOUNT PATH=/path` | Unmount the filesystem at the given path |
| `TRACE ON` | Enable per-request trace entries in the ring buffer |
| `TRACE OFF` | Disable tracing |
| `TRACE DUMP` | Dump the last 256 trace entries to SYSPRINT |
| `REBUILD` | Force a full free-block and free-inode cache rebuild on all mounted disks |
| `SHUTDOWN` | Orderly shutdown (same as P UFSD) |

Examples:

```
F UFSD,STATS
F UFSD,MOUNT DD=WEBROOT,PATH=/www
F UFSD,MOUNT DD=USRHOME,PATH=/u,MODE=RW,OWNER=IBMUSER
F UFSD,UNMOUNT PATH=/www
F UFSD,TRACE ON
F UFSD,TRACE DUMP
```

---

### Mounting Filesystems

All filesystem access goes through a mount point. Paths are Unix-style, with
`/` as the root separator. File names may be up to 59 characters long.

Mount a read-only web content filesystem:

```
F UFSD,MOUNT DD=WEBROOT,PATH=/www,MODE=RO
```

Mount a user home directory, writable only by `IBMUSER`:

```
F UFSD,MOUNT DD=USRHOME,PATH=/u/ibmuser,MODE=RW,OWNER=IBMUSER
```

Mount a shared scratch area (writable by any authenticated user):

```
F UFSD,MOUNT DD=SCRATCH,PATH=/tmp,MODE=RW
```

A filesystem can be unmounted only when no sessions have open file descriptors
on it:

```
F UFSD,UNMOUNT PATH=/www
```

---

## Using libufs in C Programs

Include `libufs.h` and link against the `LIBUFS` NCALIB member.

```c
#include "libufs.h"
```

### Session Lifecycle

Every program that uses UFSD opens a session with `ufsnew()` and releases it
with `ufsfree()`.

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

---

### File I/O

Open modes follow `fopen(3)` conventions: `"r"`, `"w"`, `"a"`, `"rb"`,
`"wb"`. Note that UFSD filesystems are EBCDIC; text and binary mode have the
same effect on MVS.

```c
UFSFILE *fp;
char     buf[256];
UINT32   n;

/* Open for writing */
fp = ufs_fopen(ufs, "/www/hello.txt", "w");
if (fp == NULL) {
    /* file not found, path invalid, or read-only filesystem */
    return 1;
}

ufs_fputs("Hello, World!\n", fp);
ufs_fclose(&fp);

/* Open for reading */
fp = ufs_fopen(ufs, "/www/hello.txt", "r");
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

---

### Directory Listing

```c
UFSDDESC *ddesc;
UFSDLIST *entry;

/* List all entries under /www matching "*.html" */
ddesc = ufs_diropen(ufs, "/www", "*.html");
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
ddesc = ufs_diropen(ufs, "/www", NULL);
```

---

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
```

---

### Error Handling

File functions return `NULL` on failure. Integer functions return a negative
value or zero. No per-call error code is available in Phase 1; use
`ufs_ferror()` on a file handle after a failed read or write:

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

---

### Complete Example

```c
/*
 * Copy a local MVS dataset member into a UFSD filesystem.
 * Compile with: cc -I./include -o copy copy.c
 * Link against: LIBUFS NCALIB member
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

    dst = ufs_fopen(ufs, "/www/upload.bin", "wb");
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

---

## Using libufs from Assembler

libufs functions follow the standard IBM OS/VS C linkage convention:

- Register 1 points to a **parameter list** — a list of fullword addresses,
  one per argument.
- Each address in the parameter list points to a fullword containing the
  actual argument value (pointer or integer).
- The high bit of the last address in the list is set (variable-length plist
  indicator).
- The return value (pointer or integer) is in Register 15.
- Register 13 must point to an 18-word (72-byte) save area on entry.
- Registers 14, 15, 0, and 1 are not preserved across the call.

### Example: Open a Session and Read a File

```asm
*----------------------------------------------------------------*
* UFSDEMO -- read /www/index.html using libufs                   *
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
         L     R15,=V(UFS_FOPEN)
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
         L     R15,=V(UFS_FREAD)
         BALR  R14,R15
         ST    R15,NREAD          bytes read returned in R15
*
*-- ufs_fclose(&filhdl) ---------------------------------------*
*   void ufs_fclose(UFSFILE **file)
         LA    R2,FILHDL          address of UFSFILE* variable
         ST    R2,P_FPP           plist slot: UFSFILE** value
         LA    R1,PL_FCLOSE
         L     R15,=V(UFS_FCLOSE)
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
FILEPATH DC    C'/www/index.html',X'00'
OPENMODE DC    C'r',X'00'
READBUF  DS    CL256
SAVEAREA DS    18F
         END   UFSDEMO
```

### Notes for Assembler Callers

**`ufsnew` has no parameters.** Set R1 to zero or leave it undefined; the
function does not dereference R1.

**`ufsfree` and `ufs_fclose` take a pointer-to-pointer** (`UFS **` and
`UFSFILE **`). Pass the address of your handle variable, not the handle value
itself. After the call the handle is set to NULL.

**`ufs_fread` and `ufs_fwrite` return the number of items** (not bytes) when
`size > 1`; with `size = 1` the return value is the byte count. The return is
in R15 as a fullword unsigned integer.

**String arguments** (path, mode) must be NUL-terminated EBCDIC strings. Use
`DC C'...',X'00'` in your assembler source.

**The 4 K buffer path** is used transparently when `ufs_fread` / `ufs_fwrite`
transfers more than 248 bytes in a single call. No special action is required
from the caller; the SSI router handles the copy through the CSA buffer pool.
