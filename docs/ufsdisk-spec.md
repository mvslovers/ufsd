# UFS370 Disk Image Format Specification

**Version 1.0 — March 2026**

Complete specification of the UFS370 on-disk format as implemented in
[ufs370](https://github.com/mvslovers/ufs370) and
[ufs370-tools](https://github.com/mvslovers/ufs370-tools).
Sufficient to implement a standalone toolchain (mkufs, fsck, mount)
on macOS, Linux, or any POSIX platform — no MVS dependency required.

---

## 1. Overview

UFS370 is a Unix V7-style filesystem originally designed for MVS 3.8j
running on Hercules (S/370 emulation). The on-disk format is stored in
a flat binary image ("BDAM dataset" on MVS, plain file on host systems).

All multi-byte integers are **big-endian** (S/370 native byte order).
All strings are **EBCDIC** on MVS, but a host-side tool would typically
convert to/from ASCII at the I/O boundary.

The image is divided into fixed-size **sectors** (physical blocks).
Sector 0 is the boot block, sector 1 is the superblock, sectors 2..N
are the inode list, and sectors N+1..end are data blocks.

---

## 2. Disk Geometry

### 2.1 Sector Size (Block Size)

The sector size is set at format time and recorded in the boot block.
Valid values:

| Block Size | Inodes/Block | Dirents/Block | Typical Use |
|-----------|-------------|--------------|-------------|
| 512 | 4 | 8 | Minimal, testing |
| 1024 | 8 | 16 | Default (FORMAT tool) |
| 2048 | 16 | 32 | Medium |
| 4096 | 32 | 64 | **Recommended for 3390** |
| 8192 | 64 | 128 | Maximum |

Block size must be a multiple of 512 and at most 8192.
The recommended size for modern use is **4096 bytes**.

### 2.2 Disk Layout

```
Sector   Contents
------   --------
  0      Boot Block (8 bytes) + Boot Extension (256 bytes)
  1      Superblock (512 bytes)
  2      Inode List start (sector 2 through datablock_start - 1)
  ...    Inode blocks
  N      First data block (sector N = sb.datablock_start_sector)
  ...    Data blocks
  END    Last sector (sector sb.volume_size - 1)
```

### 2.3 Sizing Formulas

Given a raw image of `total_blocks` sectors at `blksize` bytes each:

```
image_size_bytes   = total_blocks × blksize
inode_blocks       = floor(total_blocks / inodes_per_block × inode_pct / 100 + 0.5)
                     minimum 2
datablock_start    = 2 + inode_blocks
total_inodes       = inode_blocks × (blksize / 128)
total_data_blocks  = total_blocks - datablock_start
usable_bytes       = total_data_blocks × blksize
```

### 2.4 MVS DASD Capacity Reference

When allocating a BDAM dataset on MVS, the number of blocks depends on
the DASD device type and block size. This table shows blocks per track
and maximum blocks per single-volume dataset:

| Device | Track Size | Blk 512 | Blk 1024 | Blk 2048 | Blk 4096 | Blk 8192 |
|--------|-----------|---------|---------|---------|---------|---------|
| 3330 | 13,030 | 20 | 13 | 7 | 4 | 2 |
| 3340 | 8,368 | 12 | 8 | 4 | 2 | 1 |
| 3350 | 19,069 | 24 | 16 | 9 | 5 | 3 |
| 3375 | 35,616 | 32 | 22 | 14 | 8 | 5 |
| 3380 | 47,476 | 32 | 24 | 16 | 10 | 6 |
| 3390 | 56,664 | 32 | 27 | 18 | 12 | 7 |

Tracks per cylinder and cylinders per volume vary by device model.
For host-side images, there is no DASD constraint — use any size.

**Recommended host image sizes:**

| Use Case | Image Size | Blocks (4K) | Approx. Usable |
|----------|-----------|-------------|----------------|
| Minimal test | 1 MB | 256 | ~900 KB |
| Small website | 10 MB | 2,560 | ~9 MB |
| Medium project | 50 MB | 12,800 | ~45 MB |
| Large filesystem | 200 MB | 51,200 | ~180 MB |

---

## 3. Boot Block (Sector 0)

### 3.1 Boot Block Header (Offset 0x00, 8 bytes)

```
Offset  Size  Type    Field          Description
------  ----  ------  -----------    -----------
0x00    2     UINT16  type           Filesystem type
0x02    2     UINT16  check          Checksum: ~type (type + check == 0xFFFF)
0x04    2     UINT16  blksize        Physical block size in bytes
0x06    2     UINT16  padding        Alignment (zero)
```

Type values:
- `0x0000` — Raw/unformatted disk
- `0x0001` — Unknown disk type
- `0x0002` — UFS filesystem (**the only valid value for formatted disks**)

Validation: `boot.type + boot.check == 0xFFFF` and `boot.type == 2`.

### 3.2 Boot Block Extension (Offset 0x08, 256 bytes)

Present only on **version 1** disks (version field == 1). On version 0
disks, the area after the 8-byte header is undefined.

```
Offset  Size  Type      Field          Description
------  ----  --------  -----------    -----------
0x08    8     time64_t  create_time    Disk creation time (ms since epoch)
0x10    8     time64_t  update_time    Last update time (ms since epoch)
0x18    1     UINT8     version        Disk format version (0 or 1)
0x19    3     BYTE[3]   unused1        Reserved (zero)
0x1C    4     UINT32    unused2        Reserved (zero)
0x20    224   BYTE[224] unused3        Reserved (zero)
```

Total boot block area: 0x100 (256 bytes). Remainder of sector 0
(up to blksize) is zero-filled.

---

## 4. Superblock (Sector 1, 512 bytes)

```
Offset  Size  Type    Field                   Description
------  ----  ------  --------------------    -----------
0x000   4     UINT32  datablock_start_sector  First data block sector number
0x004   4     UINT32  volume_size             Total sectors on disk
0x008   1     BYTE    lock_freeblock          Free block lock (runtime only)
0x009   1     BYTE    lock_freeinode          Free inode lock (runtime only)
0x00A   1     BYTE    modified                Modified flag (runtime)
0x00B   1     BYTE    readonly                Read-only flag
0x00C   4     time_t  update_time             Update timestamp (V0: seconds since epoch, V1: 0)
0x010   4     UINT32  total_freeblock         Total free data blocks
0x014   4     UINT32  total_freeinode         Total free inodes
0x018   4     time_t  create_time             Create timestamp (V0: seconds since epoch, V1: 0)
0x01C   4     UINT32  nfreeblock              Entries in freeblock[] cache
0x020   204   UINT32[51]  freeblock           Free block cache (sector numbers)
0x0EC   4     UINT32  nfreeinode              Entries in freeinode[] cache
0x0F0   256   UINT32[64]  freeinode           Free inode cache (inode numbers)
0x1F0   4     UINT32  inodes_per_block        Inodes per sector (blksize / 128)
0x1F4   4     UINT32  blksize_shift           log2(blksize)
0x1F8   4     UINT32  ilist_sector            Inode list start sector (always 2)
0x1FC   4     UINT32  unused3                 Reserved (zero)
```

Total: 0x200 (512 bytes). Remainder of sector 1 is zero-filled.

**Note on timestamps:** On version 1 disks (boot extension present),
`update_time` and `create_time` in the superblock are set to 0.
The authoritative timestamps are in the boot extension (64-bit, ms).
On version 0 disks, the superblock timestamps are 32-bit `time_t`
(seconds since Unix epoch), which overflow after year 2106.

---

## 5. Inodes (Sectors 2 through datablock_start - 1)

### 5.1 On-Disk Inode (128 bytes)

```
Offset  Size  Type       Field      Description
------  ----  ---------  --------   -----------
0x00    2     UINT16     mode       File type + permission bits
0x02    2     UINT16     nlink      Link count (dirs: child count + 2)
0x04    4     UINT32     filesize   File size in bytes
0x08    8     UFSTIMEV   ctime      Creation time
0x10    8     UFSTIMEV   mtime      Modification time
0x18    8     UFSTIMEV   atime      Access time
0x20    9     char[9]    owner      Owner user ID + NUL
0x29    9     char[9]    group      Group name + NUL
0x32    2     UINT16     codepage   Code page (0 = default EBCDIC)
0x34    76    UINT32[19] addr       Block address list
```

Total: 0x80 (128 bytes). Exactly `blksize / 128` inodes per sector.

### 5.2 UFSTIMEV — Dual-Format Timestamp (8 bytes)

The 8-byte timestamp field is a **union** supporting two formats:

**Version 1 (legacy):** `UFSTIMEV1`
```
Offset  Size  Type    Field      Description
0x00    4     UINT32  seconds    Seconds since Unix epoch (1970-01-01)
0x04    4     UINT32  useconds   Microseconds (0–999999)
```

**Version 2 (current):** `UFSTIMEV2` = `utime64_t` = `mtime64_t`
```
Offset  Size  Type      Field    Description
0x00    8     uint64_t  value    Milliseconds since Unix epoch (big-endian)
```

**Detection rule:** If the second 32-bit word (offset +4) is less than
1,000,000, it is a V1 timestamp (the useconds field of a valid V1
timestamp is always < 1,000,000). Otherwise, treat the full 8 bytes as
a V2 64-bit millisecond value.

**V1 to V2 conversion:**
```
v2_ms = (uint64_t)v1.seconds × 1000 + v1.useconds / 1000
```

**V2 to V1 conversion:**
```
v1.seconds  = (uint32_t)(v2_ms / 1000)
v1.useconds = (uint32_t)(v2_ms % 1000) × 1000
```

### 5.3 Inode Numbering

- Inode 0: Reserved (never used)
- Inode 1: BALBLK (monument, never used)
- Inode 2: **Root directory** (always)
- Inode 3+: Available for allocation

Given an inode number `ino`:
```
sector = ilist_sector + (ino - 1) / inodes_per_block
offset = ((ino - 1) % inodes_per_block) × 128
```

Note: inode 0 occupies the first 128-byte slot in sector 2, but is
never allocated. Inode 1 (BALBLK) occupies the second slot. Inode 2
(root) is the third slot.

### 5.4 Mode Field (File Type + Permissions)

```
Bits     Mask     Value   Meaning
------   ------   ------  -------
15..12   0xF000           File type
                  0x1000  FIFO (named pipe, BSD extension)
                  0x2000  Character device
                  0x3000  Multiplexed char special (obsolete)
                  0x4000  Directory
                  0x6000  Block device
                  0x7000  Multiplexed block special (obsolete)
                  0x8000  Regular file
                  0xA000  Symbolic link (BSD extension)
                  0xC000  Socket (BSD extension)
11..9    0x0E00           Owner permissions (rwx)
8..6     0x01C0           Group permissions (rwx)
5..3     0x0038           Other permissions (rwx)
2..0     0x0007           (unused on UFS370)
```

Default permission mask: `0755` (octal) = `0x01ED`.

### 5.5 Block Address List (addr[0..18])

| Index | Purpose |
|-------|---------|
| 0–15 | Direct block addresses (16 × blksize bytes) |
| 16 | Single indirect block (points to block of UINT32 addresses) |
| 17 | Double indirect block |
| 18 | Triple indirect block |

**Maximum file sizes (4096-byte blocks):**

| Addressing | Blocks | Max Size |
|-----------|--------|----------|
| Direct only (addr[0..15]) | 16 | 64 KB |
| + Single indirect | 16 + 1024 | 4.06 MB |
| + Double indirect | + 1,048,576 | 4.00 GB |
| + Triple indirect | + 1,073,741,824 | 4.00 TB (theoretical) |

An indirect block is a sector filled with UINT32 block addresses.
Entries per indirect block: `blksize / sizeof(UINT32)` = `blksize / 4`.

An address value of 0 means "not allocated" (sparse file / hole).

---

## 6. Directory Entries (64 bytes)

```
Offset  Size  Type     Field          Description
------  ----  -------  -----------    -----------
0x00    4     UINT32   inode_number   Inode number (0 = free/deleted entry)
0x04    60    char[60] name           Filename + NUL (max 59 chars)
```

Total: 0x40 (64 bytes). Entries per block: `blksize / 64`.

Directory data blocks contain a flat array of these entries. An entry
with `inode_number == 0` is free and can be reused.

Every directory contains at least two entries:
- `.` (self, points to own inode)
- `..` (parent, points to parent inode; root's `..` points to itself)

### 6.1 Path Resolution

Paths are `/`-separated. Resolution starts at the root inode (2) for
absolute paths, or at the session's current working directory inode
for relative paths. Each component is looked up by scanning directory
entries (linear search). `UFS_NAME_MAX` = 59 characters.
`UFS_PATH_MAX` = 256 characters.

---

## 7. Free Block Management

Free blocks are managed via a **chained free-block list** rooted in
the superblock's `freeblock[]` cache.

### 7.1 Superblock Free Cache

`sb.freeblock[0..nfreeblock-1]` contains up to 51 free block numbers.
When a block is needed, pop from this cache. When empty, the first
entry in the cache points to a **chain block** on disk containing the
next batch.

### 7.2 Chain Block Format

A chain block has the same structure as the first 208 bytes of the
superblock's free area:

```
Offset  Size  Type       Field       Description
------  ----  ---------  ---------   -----------
0x00    4     UINT32     nfreeblock  Number of entries (max 51)
0x04    204   UINT32[51] freeblock   Free block numbers
```

`freeblock[0]` is the next chain block. When `nfreeblock` entries are
consumed, read the block at `freeblock[0]` to refill the cache.

### 7.3 Allocation Algorithm

```
1. If sb.nfreeblock > 0:
     block = sb.freeblock[--sb.nfreeblock]
     return block
2. Else:
     chain_block = sb.freeblock[0]
     read chain_block into buffer
     copy buffer's freeblock[] and nfreeblock into sb
     goto step 1
```

### 7.4 Deallocation

Push the freed block number onto the superblock cache:
```
1. If sb.nfreeblock < 51:
     sb.freeblock[sb.nfreeblock++] = freed_block
2. Else:
     write sb.freeblock[] to freed_block (it becomes a chain block)
     sb.nfreeblock = 1
     sb.freeblock[0] = freed_block
```

---

## 8. Free Inode Management

Similar to free blocks. `sb.freeinode[0..nfreeinode-1]` caches up to
64 free inode numbers. Unlike blocks, there is no chain — when the
cache is empty, the inode list is scanned linearly for `mode == 0`
entries to refill.

---

## 9. Multi-Disk and Mounting

### 9.1 DD Name Convention

On MVS, disks are referenced by DD names: `UFSDISK0` through
`UFSDISK9` (maximum 10 disks). `UFSDISK0` is always the root
filesystem.

### 9.2 fstab

The file `/etc/fstab` on the root disk controls additional mounts.
Format (one entry per line):

```
ddname=UFSDISK1  /disk1  ufs
ddname=UFSDISK2  /data   ufs
```

Or shorthand (DD name inferred from first field if <= 8 chars, no dots):
```
UFSDISK1  /disk1  ufs
```

Fields: `ddname=NAME` or `dsname=DS.NAME`, mount path, filesystem type.
Lines starting with `#` are comments.

The mount path directory must already exist on the root disk.

### 9.3 Host-Side Mounting

For a host-side tool, multi-disk support means: open multiple image
files, mount each on a path in a virtual directory tree. The root
image provides `/`, additional images mount on subdirectories.

---

## 10. Creating a Disk Image (Host-Side Algorithm)

### 10.1 Phase 1: Zero-Fill

Create a file of `total_blocks × blksize` bytes, all zeros.

### 10.2 Phase 2: Write Boot Block

At sector 0:
```
bytes 0–1:   0x0002                    (UFS type)
bytes 2–3:   0xFFFD                    (~0x0002)
bytes 4–5:   blksize (big-endian)
bytes 6–7:   0x0000                    (padding)
bytes 8–15:  create_time (time64, ms since epoch, big-endian)
bytes 16–23: update_time (same as create_time)
byte  24:    0x01                      (version = 1)
bytes 25–255: 0x00                     (reserved)
```

### 10.3 Phase 3: Write Superblock

Calculate:
```
inode_blocks = max(2, round(total_blocks / inodes_per_block × inode_pct / 100))
datablock_start = 2 + inode_blocks
total_inodes = inode_blocks × inodes_per_block
total_data_blocks = total_blocks - datablock_start
```

Write 512 bytes at sector 1 with the `ufs_superblock` structure.
Set `update_time = 0`, `create_time = 0` (version 1 uses boot extension).

### 10.4 Phase 4: Write Inode Blocks

For each inode block (sectors 2 through datablock_start - 1):
- Fill with 0xFF (marks uninitialized)
- For each inode slot: clear to 0x00 (marks as free, `mode == 0`)
- Exception: inode 0 and inode 1 remain 0xFF (reserved)

### 10.5 Phase 5: Build Free Block Chain

Starting from `datablock_start`, build the chain:
1. Fill `sb.freeblock[]` with up to 51 block numbers
2. For remaining blocks: write chain blocks (each containing up to 51
   pointers, with `freeblock[0]` pointing to the next chain block)
3. Update `sb.total_freeblock` and `sb.nfreeblock`

### 10.6 Phase 6: Create Root Directory

1. Allocate inode 2 (root) from free inode cache
2. Allocate one data block for the root directory
3. Write root inode:
   - `mode = 0x41ED` (directory + 0755)
   - `nlink = 2`
   - `filesize = 128` (2 × 64-byte entries)
   - `addr[0] = allocated_block`
   - `ctime/mtime/atime = now` (time64 format)
   - `owner = "HERC01"` (or from RACF ACEE)
   - `group = "ADMIN"` (or from RACF ACEE)
4. Write root data block:
   - Entry 0: `inode=2, name="."`
   - Entry 1: `inode=2, name=".."` (root parent is itself)
5. Write back superblock (freeblock/freeinode caches updated)

### 10.7 Phase 7: Verify

Read back boot block, superblock, and root inode. Verify checksums,
counts, and root directory structure.

---

## 11. Reading a Disk Image (Host-Side Algorithm)

```
1. Open image file
2. Read sector 0 → validate boot.type == 2, boot.check == ~type
3. blksize = boot.blksize
4. Check boot extension version (byte 0x18)
5. Read sector 1 → superblock
6. inodes_per_block = sb.inodes_per_block (or blksize / 128)
7. Root inode = inode 2 at sector 2, offset 128
8. Resolve paths by scanning directory entries
9. Read file data via inode addr[] (direct + indirect)
```

---

## 12. Character Encoding

All strings on disk are in **EBCDIC** (IBM Code Page 037 or 273).
A host-side tool must convert:
- Filenames: EBCDIC ↔ ASCII/UTF-8
- File content: depends on file type (binary files: no conversion;
  text files: EBCDIC ↔ ASCII line-by-line)

EBCDIC newline is `0x15` (NL) or `0x25` (LF). The UFS370 FTPD
performs EBCDIC↔ASCII conversion for TYPE A (text) transfers.

The inode `codepage` field (offset 0x32) can indicate a specific code
page. Value 0 means default (system EBCDIC).

---

## 13. Byte Order Summary

**Everything is big-endian.** All UINT16, UINT32, and time64_t values
are stored in S/370 native byte order (most significant byte first).
A host-side tool on x86/ARM must byte-swap all multi-byte fields.

---

## 14. Differences from Unix V7

| Feature | Unix V7 | UFS370 |
|---------|---------|--------|
| Block size | 512 fixed | 512–8192, configurable |
| Inode size | 64 bytes | 128 bytes |
| Dirent size | 16 bytes (14-char names) | 64 bytes (59-char names) |
| Timestamps | 32-bit seconds | 8-byte union (V1: sec+usec, V2: 64-bit ms) |
| Owner/Group | UID/GID (numeric) | 8-char string names |
| Code page | n/a | UINT16 per inode |
| Boot extension | n/a | 256-byte version/timestamp block |
| Free block chain | In superblock | In superblock + chain blocks (same as V7) |
| Symbolic links | n/a | Supported (BSD extension, mode 0xA000) |

---

## Appendix A: C Structure Quick Reference

```c
/* Boot Block (8 bytes) */
struct ufs_boot {
    UINT16  type;       /* 0x0002 = UFS */
    UINT16  check;      /* ~type         */
    UINT16  blksize;    /* block size    */
    UINT16  padding;
};

/* Boot Extension (256 bytes, version >= 1) */
struct ufs_boot_ext {
    time64_t create_time;   /* ms since epoch */
    time64_t update_time;
    BYTE     version;       /* 0 or 1         */
    BYTE     unused1[3];
    UINT32   unused2;
    UINT32   unused3[56];
};

/* Superblock (512 bytes) */
struct ufs_superblock {
    UINT32  datablock_start_sector;
    UINT32  volume_size;
    BYTE    lock_freeblock;
    BYTE    lock_freeinode;
    BYTE    modified;
    BYTE    readonly;
    UINT32  update_time;        /* V0 only, V1 = 0 */
    UINT32  total_freeblock;
    UINT32  total_freeinode;
    UINT32  create_time;        /* V0 only, V1 = 0 */
    UINT32  nfreeblock;
    UINT32  freeblock[51];
    UINT32  nfreeinode;
    UINT32  freeinode[64];
    UINT32  inodes_per_block;
    UINT32  blksize_shift;
    UINT32  ilist_sector;       /* always 2 */
    UINT32  unused3;
};

/* Inode (128 bytes) */
struct ufs_dinode {
    UINT16   mode;
    UINT16   nlink;
    UINT32   filesize;
    UFSTIMEV ctime;     /* 8 bytes: V1={sec,usec} or V2=time64 */
    UFSTIMEV mtime;
    UFSTIMEV atime;
    char     owner[9];  /* EBCDIC + NUL */
    char     group[9];  /* EBCDIC + NUL */
    UINT16   codepage;
    UINT32   addr[19];
};

/* Directory Entry (64 bytes) */
struct ufs_dirent {
    UINT32  inode_number;   /* 0 = free */
    char    name[60];       /* EBCDIC + NUL, max 59 chars */
};

/* Timestamp Union (8 bytes) */
union ufs_timeval {
    struct { UINT32 seconds; UINT32 useconds; } v1;  /* usec < 1000000 */
    uint64_t v2;  /* milliseconds since epoch */
};
```

---

## Appendix B: Host-Side FUSE Mount Sketch

A FUSE filesystem driver for macOS/Linux would:

1. Open the image file as a flat binary
2. Parse boot block → get blksize
3. Parse superblock → get geometry
4. Implement FUSE callbacks:
   - `getattr`: read inode, return stat
   - `readdir`: scan directory data blocks
   - `read`: resolve inode addr[] → read data blocks
   - `write`: allocate blocks, update inode
   - `mkdir/rmdir/unlink`: manipulate directory entries + inodes
5. Convert EBCDIC filenames to UTF-8 at the FUSE boundary
6. Optionally convert file content (text mode vs binary mode)

The `codepage` field in each inode indicates which EBCDIC variant to
use for that file's content. Code page 0 = system default.
