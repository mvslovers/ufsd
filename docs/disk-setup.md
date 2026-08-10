# Disk Setup Guide

Each UFSD filesystem lives in a BDAM dataset on MVS. Before adding a mount to the Parmlib configuration, the dataset must be allocated and formatted.

There are two routes, and they produce the same bytes:

- **On MVS, with `UFSFMT`** — a batch job, no host toolchain, nothing to upload. This is the route for the root disk and for any empty filesystem. See [Formatting on MVS](#formatting-on-mvs) below.
- **On a host, with [ufsd-utils](https://github.com/mvslovers/ufsd-utils)** — create an image, fill it with files (`cp -r`), then upload it. This is the route when the disk should arrive with content already on it. That workflow is the rest of this guide, from [Overview](#overview) onwards.

## Formatting on MVS

`UFSFMT` allocates nothing and uploads nothing: it formats a dataset that already exists into an empty UFS370 filesystem with a root directory. That removes the libc370 → HTTPD → mvsMF → ufsd-utils chain that used to stand between a fresh system and the first mountable disk.

A complete job is in [`samplib/ufsfmt`](../samplib/ufsfmt) — allocation and format in one submission, with the SPACE planning table in the comments. The short version:

```
//ALLOC    EXEC PGM=IEFBR14
//DISKFILE DD  DSN=MIKEG1.UFSHOME,DISP=(NEW,CATLG),
//             UNIT=SYSDA,SPACE=(4096,256),
//             DCB=(DSORG=PS,RECFM=U,BLKSIZE=4096)
//*
//FORMAT   EXEC PGM=UFSFMT
//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//DISKFILE DD  DISP=OLD,DSN=MIKEG1.UFSHOME
//SYSPRINT DD  SYSOUT=*
//SYSTERM  DD  SYSOUT=*
//SYSIN    DD  *
   BLKSIZE  4096
   OWNER    HERC01
/*
```

### Allocation

| Parameter | Value | Notes |
|-----------|-------|-------|
| DSORG | PS | Physical Sequential |
| RECFM | U | Undefined record format |
| BLKSIZE | 4096 | Must match the `BLKSIZE` control statement |
| Space | Blocks | Primary quantity only |

**Do not specify a secondary quantity.** UFSFMT sizes the filesystem by writing zero blocks into the primary extent until the extent is exhausted, and that count becomes the volume size. Blocks in a secondary extent would never be formatted and never be reachable.

MVS rounds the allocation up to a track boundary, so the disk usually ends up slightly larger than requested. UFSFMT formats what the primary extent actually provides and reports the real count:

```
UFSFMT26I Initialized 256 blocks (1.00 MB)
```

### Control statements

Statements are read from `SYSIN`. `PARM=` is not supported — there is one source for a parameter and no precedence rules. Each keyword may appear at most once; a repeated keyword is an error, not a silent override. Keywords must be written in full.

| Keyword | Value | Default | Meaning |
|---------|-------|---------|---------|
| `BLKSIZE` | 512–8192, multiple of 512 | `4096` | Block size |
| `DDNAME` | DD name | `DISKFILE` | DD of the dataset to format |
| `INODES` | 1.0–50.0 | `10.0` | Percent of blocks for inodes |
| `OWNER` | 1–8 chars | `HERC01` | Root directory owner |
| `GROUP` | 1–8 chars | `ADMIN` | Root directory group |
| `FORCE` | — | off | Overwrite an existing UFS filesystem |
| `QUIET` | — | off | Suppress messages and the report |
| `VERBOSE` | — | off | Extra per-phase messages |
| `HELP` | — | — | Print the help text and stop |

`SYSIN` may be omitted or `DUMMY`; every default then applies, and the report shows what they were. Comments are delimited by `/*` and `*/` and must not start in column 1 of an instream `SYSIN` — that would end the input stream. Columns 73–80 are ignored.

`INODES` is not a percentage of the volume. It is a percentage of the blocks that would be needed to index everything, so at `BLKSIZE=4096` the default of 10.0 works out to roughly one inode per 10 blocks — one file per 40 KB. The inode list is never shorter than 2 blocks, which is why any disk of 2 MB or less holds at most 62 files no matter how much space is free.

### DD statements

| DD | Purpose | Required |
|----|---------|----------|
| `DISKFILE` | Dataset to format, `DISP=OLD` (name overridable via `DDNAME`) | yes |
| `SYSPRINT` | Report | yes |
| `SYSTERM` | Messages | yes |
| `SYSIN` | Control statements | no |

`DISKFILE` must be `DISP=OLD`. A `DISP=SHR` allocation is refused (`UFSFMT25E`) rather than wiping a dataset another job may be reading — and UFSD marks a `DISP=SHR` disk read-only anyway.

### Reformat protection

UFSFMT reads sector 0 before it writes anything. If the dataset already holds a UFS370 filesystem, the job stops:

```
UFSFMT05E Dataset already contains a UFS370 filesystem
UFSFMT06I   Volume size . . . 256 blocks (1.00 MB), block size 4096
UFSFMT07I   Created . . . . . 2026-06-14 09:22:41
UFSFMT08I Specify FORCE to overwrite
```

Add `FORCE` to the control statements to overwrite it. There is no undo — the first phase of the format zeroes the whole extent. A forced reformat still describes what it replaced, so the job log records it:

```
UFSFMT05W OVERWRITING AN EXISTING UFS370 FILESYSTEM (FORCE SPECIFIED)
UFSFMT06I   Volume size . . . 256 blocks (1.00 MB), block size 4096
UFSFMT07I   Created . . . . . 2026-06-14 09:22:41
```

The probe is best effort: on a dataset that has never been written, the read of sector 0 simply fails and the format proceeds, which is the case with nothing to protect.

### The report

```
UFSFMT10I UFSFMT 1.1.0 -- UFS370 disk format utility
UFSFMT26I Initialized 256 blocks (1.00 MB)
UFSFMT51I Formatted 2 index blocks, 64 inode slots
UFSFMT52I Formatted 252 data blocks
UFSFMT71I Root directory created, owner=HERC01, group=ADMIN, mode=0755

UFSFMT80I Format summary
UFSFMT81I   Dataset . . . . . MIKEG1.UFSHOME
UFSFMT82I   Block size  . . . 4096
UFSFMT83I   Total blocks  . . 256           (1.00 MB)
UFSFMT84I   Inode blocks  . . 2             (64 slots, 62 free)
UFSFMT85I   Data blocks . . . 252           (251 free)
UFSFMT86I   Root owner  . . . HERC01/ADMIN

UFSFMT90I Add to your UFSD parmlib member:
UFSFMT91I   MOUNT    DSN(MIKEG1.UFSHOME) PATH(/your/mount/point) MODE(RW) OWNER(HERC01)
```

The `MOUNT` line carries the owner the disk was formatted for; formatting with the userid the disk will be mounted for is the documented convention.

Return codes: **0** formatted, **4** `HELP` was requested (nothing was written), **8** the format did not complete. Messages and warnings go to `SYSTERM`, the report to `SYSPRINT`; `QUIET` suppresses the report but never an error.

---

The remainder of this guide covers the host-side workflow with `ufsd-utils`, which is the route to take when the disk should arrive with files already on it.

## Overview

```
Host (Linux/macOS)                MVS (Hercules)
──────────────────                ────────────────
ufsd-utils create disk.img        
ufsd-utils cp ./files disk.img:/  
ufsd-utils upload disk.img        ──▶  BDAM dataset allocated + uploaded
                                       Add to UFSDPRMx
                                       /S UFSD (or /F UFSD,MOUNT)
```

## Step 1: Create a Disk Image

```sh
ufsd-utils create root.img --size 1M
```

Output:

```
Creating root.img (1M, blksize=4096, inodes=10.0%)
  Volume size:     256 blocks (1.00 MB)
  Block size:      4096 bytes
  Inode blocks:    2 (64 inodes)
  Data blocks:     252 (free: 251)
  Root owner:      MIKE/ADMIN
  Format:          UFS370 v1 (time64 timestamps)

  Upload to MVS:  ufsd-utils upload root.img --dsn YOUR.DATASET.NAME
Done.
```

Common sizes:

| Use Case | Size | Inodes | Notes |
|----------|------|--------|-------|
| Root filesystem | 1M | 64 | Only holds mount-point directories |
| User home | 5M | 128 | Personal files |
| Web content | 10M–50M | 256–1280 | HTML, CSS, JS |
| Scratch / tmp | 5M–10M | 128–256 | Shared temporary area |

Options:

| Flag | Default | Description |
|------|---------|-------------|
| `--size` | `10M` | Image size (e.g. `1M`, `500K`, `50M`) |
| `--blksize` | 4096 | Block size (512, 1024, 2048, 4096, 8192) |
| `--inodes` | 10% | Percentage of blocks reserved for inodes |
| `--owner` | `$USER` → `HERC01` | Root directory owner (RACF userid); uppercased `$USER`, or `HERC01` if unset |
| `--group` | `ADMIN` | Root directory group |

## Step 2: Populate Content

Copy files from the host into the image:

```sh
# Copy a single file
ufsd-utils cp index.html webroot.img:/index.html

# Copy a directory tree recursively
ufsd-utils cp -r ./wwwroot/ webroot.img:/

# Create a directory
ufsd-utils mkdir webroot.img:/css
```

Text files are converted from ASCII to EBCDIC (IBM-1047), but the conversion is auto-detected from the file **extension** against a fixed allowlist (`.html`, `.htm`, `.txt`, `.css`, `.js`, `.xml`, `.json`, `.csv`, `.sh`, `.c`, `.h`, `.s`, `.md`, `.cfg`, `.conf`, `.toml`, `.yaml`, `.yml`, `.ini`, `.log`, `.jcl`, `.proc`). A file whose extension is **not** on the list (for example `.tmpl`, or a file with no extension) is copied as-is — still ASCII — and will be unreadable on MVS. Override the auto-detection explicitly:

- `-t` forces text conversion (ASCII → EBCDIC), regardless of extension.
- `-b` forces binary (copy the bytes verbatim, no conversion).

```sh
ufsd-utils cp -t config.tmpl webroot.img:/config.tmpl   # force ASCII -> EBCDIC
ufsd-utils cp -b logo.png     webroot.img:/logo.png      # copy bytes verbatim
```

Verify the contents:

```sh
ufsd-utils ls webroot.img:/
ufsd-utils ls -l webroot.img:/css
ufsd-utils cat webroot.img:/index.html
ufsd-utils info webroot.img
```

### Image Path Syntax

All commands that operate on files inside an image use the `image:path` syntax:

```
webroot.img          → root directory (/)
webroot.img:         → root directory (/)
webroot.img:/        → root directory (/)
webroot.img:/css     → /css directory
```

## Step 3: Upload to MVS

Use `ufsd-utils upload` to allocate the BDAM dataset and transfer the image in one step:

```sh
ufsd-utils upload webroot.img --dsn HTTPD.WEBROOT
```

The upload command uses the zOSMF REST API. Authentication is configured via environment variables:

```sh
export MVS_HOST=192.168.1.x
export MVS_PORT=1080
export MVS_USER=IBMUSER
export MVS_PASS=SYS1
```

Or place them in a `.env` file in the current directory.

To overwrite an existing dataset:

```sh
ufsd-utils upload webroot.img --dsn HTTPD.WEBROOT --replace
```

## Step 4: Configure and Mount

Add the dataset to your Parmlib member (`UFSDPRMx`):

```
MOUNT    DSN(HTTPD.WEBROOT)      PATH(/wwwroot)      MODE(RO)
```

If UFSD is already running, mount dynamically without restart:

```
/F UFSD,MOUNT DSN=HTTPD.WEBROOT,PATH=/wwwroot,MODE=RO
```

## MVS-Side Dataset Allocation

If you prefer to allocate the dataset manually on MVS (instead of using `ufsd-utils upload`), create a BDAM-compatible dataset with these parameters:

| Parameter | Value | Notes |
|-----------|-------|-------|
| DSORG | PS | Physical Sequential |
| RECFM | U | Undefined record format |
| BLKSIZE | 4096 | Must match the image's block size |
| Space | Blocks | Number of blocks in the image (matches the ISPF panel below) |

**ISPF 3.2 panel values:**

```
Space units  . . 3         (1=CYL, 2=TRK, 3=BLK)
Primary qty  . . 256       (= number of blocks in image)
Block size . . . 4096
Record format  . U
Record length  . 0
Data set org . . PS
```

Then upload the image as binary using FTP, IND$FILE, or zowe CLI:

```sh
# FTP
ftp mvs-host
binary
put webroot.img 'HTTPD.WEBROOT'

# zowe CLI
zowe files upload file-to-data-set webroot.img "HTTPD.WEBROOT" --binary
```

## Inspecting Images

```sh
# Superblock and summary info
ufsd-utils info webroot.img

# Directory listing (like ls -la)
ufsd-utils ls -l webroot.img:/

# Read a file to stdout
ufsd-utils cat webroot.img:/index.html

# Remove files
ufsd-utils rm webroot.img:/old-file.txt
ufsd-utils rmdir webroot.img:/empty-dir
```
