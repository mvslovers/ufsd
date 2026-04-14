# Disk Setup Guide

Each UFSD filesystem lives in a BDAM dataset on MVS. Before adding a mount to the Parmlib configuration, the dataset must be allocated and formatted. This guide covers the complete workflow using [ufsd-utils](https://github.com/mvslovers/ufsd-utils), the host-side command-line tool.

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
| `--size` | — | Image size (e.g. `1M`, `500K`, `50M`) |
| `--blksize` | 4096 | Block size (512, 1024, 2048, 4096, 8192) |
| `--inodes` | 10% | Percentage of blocks reserved for inodes |
| `--owner` | `$USER` | Root directory owner (RACF userid) |
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

Files are automatically converted from ASCII to EBCDIC (IBM-1047). Binary files are copied as-is when using `--binary`.

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
export MVS_PORT=8080
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
MOUNT    DSN(HTTPD.WEBROOT)      PATH(/www)          MODE(RW)
```

If UFSD is already running, mount dynamically without restart:

```
/F UFSD,MOUNT DSN=HTTPD.WEBROOT,PATH=/www,MODE=RW
```

## MVS-Side Dataset Allocation

If you prefer to allocate the dataset manually on MVS (instead of using `ufsd-utils upload`), create a BDAM-compatible dataset with these parameters:

| Parameter | Value | Notes |
|-----------|-------|-------|
| DSORG | PS | Physical Sequential |
| RECFM | U | Undefined record format |
| BLKSIZE | 4096 | Must match the image's block size |
| Space | Tracks or Cylinders | Calculated from image size |

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
