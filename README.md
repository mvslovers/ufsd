# UFSD — Virtual Filesystem Daemon for MVS 3.8j

> **Experimental — not for production use.**
> UFSD is under active development. The on-disk format and wire protocol may
> change without notice. Data loss can and will happen. Use it for
> experimentation and development only.

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
  - [PARMLIB Configuration](#3-parmlib-configuration)
  - [Disk Datasets](#4-disk-datasets)
  - [Started Task User](#5-started-task-user)
- [Operating UFSD](#operating-ufsd)
  - [Starting and Stopping](#starting-and-stopping)
  - [Operator Commands](#operator-commands)
  - [Mounting Filesystems](#mounting-filesystems)
- [Known Limitations](#known-limitations)
- [Using libufs in C Programs](#using-libufs-in-c-programs)
  - [Session Lifecycle](#session-lifecycle)
  - [File I/O](#file-io)
  - [Directory Listing](#directory-listing)
  - [Seek and Sync](#seek-and-sync)
  - [Session User Identity](#session-user-identity)
  - [Error Handling](#error-handling)
  - [Complete Example](#complete-example)
- [Using libufs from Assembler](#using-libufs-from-assembler)

---

## Installation

### From a GitHub Release

**Prerequisites:** Hercules with TK4-, TK5, or MVS/CE; FTP or zowe CLI access.

Each release provides three archives:

| Archive | Contents |
|---------|----------|
| `ufsd-<version>-load.tar.gz` | Load modules (UFSD, UFSDSSIR, UFSDCLNP, …) as XMIT |
| `ufsd-<version>-lib-headers.tar.gz` | `libufs.h` and `ufsdrc.h` for compile-time use |
| `ufsd-<version>-lib-modules.tar.gz` | `LIBUFS` NCALIB member for link-time use |

1. Download `ufsd-<version>-load.tar.gz` from the [Releases] page and extract it.
   You will find a `.XMI` file (XMIT archive of the LOAD PDS).

2. Upload it to MVS as a binary sequential dataset. Allocate the target
   dataset first (RECFM=FB, LRECL=80, BLKSIZE=3120 or similar), then transfer:

   **IND$FILE** (TSO file transfer from a 3270 terminal emulator):
   upload the file in binary mode to `IBMUSER.UFSD.XMIT`.

   **zowe CLI** (pre-allocate the dataset first, then upload):
   ```sh
   zowe files upload file-to-data-set ufsd-1.0.0-dev-load.xmi "IBMUSER.UFSD.XMIT" --binary
   ```

   **FTP** (binary mode):
   ```sh
   ftp mvs-host
   binary
   put ufsd-1.0.0-dev-load.xmi 'IBMUSER.UFSD.XMIT'
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
- c2asm370, GNU Make, Python 3.12+
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
MBT_MVS_DEPS_HLQ=IBMUSER.DEPS   # HLQ for dependency datasets
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
make package      # produces three archives + package.toml:
                  #   ufsd-<version>-load.tar.gz        (UFSD, UFSDSSIR, … load modules)
                  #   ufsd-<version>-lib-headers.tar.gz (libufs.h)
                  #   ufsd-<version>-lib-modules.tar.gz (LIBUFS NCALIB member)
                  # also installs libufs into the local mbt package cache
                  # so dependent projects can bootstrap it without a GitHub release
```

---

### Installing libufs for Consumers

Programs that call libufs at compile time need the header; at link time they
need the NCALIB member.

1. Download `ufsd-<version>-lib-headers.tar.gz` from the [Releases] page.
   Extract `libufs.h` and `ufsdrc.h` into your project's `include/` (or `contrib/`).

2. Download `ufsd-<version>-lib-modules.tar.gz`, upload, and `RECEIVE` it to
   obtain the NCALIB dataset. Add it to your link JCL `SYSLIB` concatenation.

   With mbt, declare the dependency in your `project.toml`:

   ```toml
   [dependencies]
   "mvslovers/ufsd" = ">=1.0.0-dev"
   ```

   `make bootstrap` then fetches and installs everything automatically.

---

## System Configuration

### 1. APF Authorization

UFSD allocates CSA storage (subpool 241, key 0) and operates in supervisor
state. The STC authorizes itself at startup, so explicit APF authorization of
the load library is typically not required on TK4- or TK5.

On hardened systems (RAKF or RACF with strict APF checking) you may need to
authorize the load library explicitly. Use whatever method is standard on your
MVS system for authorizing a load library (e.g. adding it to the APF list in
PARMLIB and re-IPLing, or via a dynamic authorization command if your system
supports one).

---

### 2. JCL Procedure

Copy `samplib/ufsd` from this repository to `SYS2.PROCLIB(UFSD)`.
Adjust the `STEPLIB` DSN to match your load library name:

```jcl
//UFSD     PROC M=UFSDPRM0,
//            D='SYS2.PARMLIB'
//*
//* UFSD - Filesystem Server STC Procedure
//*
//* Starting:    /S UFSD
//*              /S UFSD,M=UFSDPRM1       (alternate config member)
//*              /S UFSD,D='MY.PARMLIB'   (alternate parmlib dataset)
//*
//CLEANUP  EXEC PGM=UFSDCLNP
//STEPLIB  DD  DSN=IBMUSER.UFSD.V1R0M0D.LOAD,DISP=SHR
//UFSD     EXEC PGM=UFSD,REGION=4M,TIME=1440
//STEPLIB  DD  DSN=IBMUSER.UFSD.V1R0M0D.LOAD,DISP=SHR
//SYSUDUMP DD  SYSOUT=*
//UFSDPRM  DD  DSN=&D(&M),DISP=SHR,FREE=CLOSE
```

The `CLEANUP` step runs `UFSDCLNP` before the main step, which deregisters
any stale SSCT entry left over from a previous abend. Restarts are safe
without manual intervention.

Disk mounts are configured via the PARMLIB member (see next section) — no DD
cards for filesystems are needed.

**Using SYS1.PARMLIB:** either edit the `D=` default in the PROC, or pass the
override at start time:

```
S UFSD,D='SYS1.PARMLIB'
S UFSD,M=UFSDPRM1,D='MY.PARMLIB'
```

---

### 3. PARMLIB Configuration

Create member `UFSDPRM0` in `SYS2.PARMLIB` (or whichever dataset the PROC
points to). A sample is provided in `samplib/ufsdprm0`. This member defines
the root filesystem and all mounts:

```
/* SYS2.PARMLIB(UFSDPRM0) */

/* Root filesystem */
ROOT     DSN(UFSD.ROOT)

/* Read-only web content */
MOUNT    DSN(HTTPD.WEBROOT)      PATH(/wwwroot)      MODE(RO)

/* User home directory, writable only by IBMUSER */
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)

/* Shared scratch area, writable by any authenticated user */
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```

**Keywords:**

| Keyword | Parameters | Description |
|---------|-----------|-------------|
| `ROOT` | `DSN` \[, `SIZE`, `BLKSIZE`\] | Root filesystem; SIZE and BLKSIZE are used for auto-creation |
| `MOUNT` | `DSN`, `PATH`, `MODE`, `OWNER` | Additional filesystem |

**Parameters:**

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| `DSN(name)` | dataset name | — | BDAM dataset to mount |
| `PATH(/path)` | absolute path | `/` (ROOT only) | Mount point |
| `SIZE(n)` | `500K`, `1M`, … | — | Root disk size (ROOT only) |
| `BLKSIZE(n)` | 512–8192 | `4096` | Block size in bytes (ROOT only) |
| `MODE(RO\|RW)` | `RO` or `RW` | `RO` | Read-only or read-write |
| `OWNER(userid)` | RACF userid | — | Restrict writes to this userid |

Lines beginning with `/*` are comments (terminated by `*/`).

**Root filesystem:** UFSD requires a root image mounted at `/`. A size of 1 MB
is sufficient for the root (it only needs to hold mount-point directories).
Create it with `ufsd-utils` as described in the [Disk Datasets](#4-disk-datasets)
section. Automatic root disk creation on first start is planned for a future
release.

---

### 4. Disk Datasets

Each UFSD filesystem lives in a BDAM dataset. Before adding a mount to the
PARMLIB configuration, the dataset must be **allocated and formatted**.

#### Step 1: Create the filesystem image with ufsd-utils

`ufsd-utils` is a host-side command-line tool (separate repository) that
creates and inspects UFS370 disk images:

```
$ ufsd-utils create root.img --size 1M
Creating root.img (1M, blksize=4096, inodes=10.0%)
  Volume size:     256 blocks (1.00 MB)
  Block size:      4096 bytes
  Inode blocks:    2 (64 inodes)
  Data blocks:     252 (free: 251)
  Root owner:      MIGO/ADMIN
  Format:          UFS370 v1 (time64 timestamps)

  MVS Dataset Allocation (DSORG=DA, BDAM):

    DCB=(DSORG=DA,BLKSIZE=4096)  SPACE=(4096,(256))

    JCL (recommended — ISPF/RPF panel cannot set DSORG=DA):
      //ALLOC  EXEC PGM=IEFBR14
      //DISK   DD DSN=your.dataset.name,
      //          DISP=(NEW,CATLG,DELETE),UNIT=SYSDA,
      //          SPACE=(4096,(256)),
      //          DCB=(DSORG=DA,BLKSIZE=4096)

    Transfer: FTP binary PUT into pre-allocated dataset
Done.
```

`ufsd-utils` prints the exact JCL and SPACE parameters to use for the MVS
allocation, derived from the image geometry. Use those values — do not guess.

> A native MVS formatting tool is planned and will be added to the UFSD
> package in a future release. Until then, `ufsd-utils` on the host is the
> only supported way to create new filesystem images.

#### Step 2: Allocate the MVS dataset

Use the JCL printed by `ufsd-utils` (with your actual dataset name and volume):

```jcl
//ALLOC  EXEC PGM=IEFBR14
//DISK   DD DSN=HTTPD.WEBROOT,
//          DISP=(NEW,CATLG,DELETE),UNIT=SYSDA,VOL=SER=PUB000,
//          SPACE=(4096,(256)),
//          DCB=(DSORG=DA,BLKSIZE=4096)
```

ISPF/RPF dataset allocation panels cannot set `DSORG=DA` — JCL is required.

#### Step 3: Upload the image

Transfer the `.img` file to the pre-allocated dataset in binary mode:

```sh
# zowe CLI
zowe files upload file-to-data-set root.img "HTTPD.WEBROOT" --binary

# or FTP (binary mode)
ftp mvs-host
binary
put root.img 'HTTPD.WEBROOT'
```

The dataset is now ready to be listed in `UFSDPRM0` and mounted.

---

### 5. Started Task User

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
| `MOUNT DSN=dsn,PATH=/path` | Mount a dataset by name (DYNALLOC) |
| `MOUNT ...,MODE=RO` | Mount read-only (write requests return ROFS) |
| `MOUNT ...,MODE=RW` | Mount read-write (default) |
| `MOUNT ...,OWNER=userid` | Restrict write access to the named userid |
| `MOUNT LIST` | List all currently mounted filesystems |
| `UNMOUNT PATH=/path` | Unmount the filesystem at the given path |
| `TRACE ON` | Enable per-request trace entries in the ring buffer |
| `TRACE OFF` | Disable tracing |
| `TRACE DUMP` | Dump the last 256 trace entries to SYSPRINT |
| `REBUILD` | Force a full free-block and free-inode cache rebuild on all mounted disks |
| `HELP` | Print a summary of available commands |
| `SHUTDOWN` | Orderly shutdown (same as P UFSD) |

Examples:

```
F UFSD,STATS
F UFSD,SESSIONS
F UFSD,MOUNT DSN=HTTPD.WEBROOT,PATH=/wwwroot,MODE=RO
F UFSD,MOUNT DSN=IBMUSER.UFSHOME,PATH=/u/ibmuser,MODE=RW,OWNER=IBMUSER
F UFSD,MOUNT LIST
F UFSD,UNMOUNT PATH=/wwwroot
F UFSD,TRACE ON
F UFSD,TRACE DUMP
F UFSD,REBUILD
F UFSD,HELP
```

---

### Mounting Filesystems

Static mounts are defined in the PARMLIB member and activated automatically at
startup. Use `F UFSD,MOUNT` to add additional filesystems at runtime without
restarting.

Paths are Unix-style with `/` as the root separator. File names may be up to
59 characters long.

Mount a dataset dynamically:

```
F UFSD,MOUNT DSN=HTTPD.WEBROOT,PATH=/wwwroot,MODE=RO
F UFSD,MOUNT DSN=IBMUSER.UFSHOME,PATH=/u/ibmuser,MODE=RW,OWNER=IBMUSER
F UFSD,MOUNT DSN=UFSD.SCRATCH,PATH=/tmp,MODE=RW
```

A filesystem can be unmounted only when no sessions have open file descriptors
on it:

```
F UFSD,UNMOUNT PATH=/wwwroot
```

---

## Known Limitations

- **On-disk format is not finalized.** The UFS370 v1 format may change in
  future releases. Do not rely on disk images being portable across versions.
- **64 concurrent sessions.** The session table is fixed at 64 slots. If all
  slots are occupied, `ufsnew()` returns NULL.
- **256 open file descriptors.** The global file table limits the total number
  of simultaneously open files across all sessions.
- **No symbolic links or hard links.** Only regular files and directories are
  supported.
- **No file locking.** Concurrent writes to the same file from different
  sessions are not serialized.

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

/* Stat a file or directory (metadata without open) */
UFSDLIST info;
if (ufs_stat(ufs, "/www/index.html", &info) == 0) {
    /* info.filesize, info.attr, info.owner, info.mtime, ... */
}
```

---

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

---

### Session User Identity

Set the session owner for per-user permission checks on OWNER-restricted
mounts. Typically called after `ufsnew()`:

```c
ufs_setuser(ufs, "IBMUSER", "SYS1");
```

---

### Error Handling

File functions return `NULL` on failure. Integer functions return a non-zero
value on error. Use `ufs_last_rc()` to retrieve the UFSD return code after
a failed operation:

```c
if (ufs_mkdir(ufs, "/u/ibmuser/docs") != 0) {
    int rc = ufs_last_rc(ufs);
    /* rc is one of the UFSD_RC_* codes from ufsdrc.h, e.g.:
     *   UFSD_RC_NOFILE   (28) — parent path not found
     *   UFSD_RC_EXIST    (32) — directory already exists
     *   UFSD_RC_ROFS     (68) — read-only filesystem
     *   UFSD_RC_EACCES   (72) — permission denied
     */
}
```

For file I/O, use `ufs_ferror()` on the file handle after a short read
or write:

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
