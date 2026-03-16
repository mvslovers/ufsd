# AP-3a: Parmlib Configuration + Mount Model

**Status:** Planned  
**Depends on:** AP-2a (complete)  
**Branch:** `feature/ap-3a`

---

## Goal

Replace DD-card-based disk configuration with a Parmlib-driven mount
model. UFSD reads `SYS1.PARMLIB(UFSDPRMx)`, opens datasets dynamically
via SVC 99 (DYNALLOC), auto-creates a root disk on first start, and
enforces per-mount access control (RO/RW + owner check).

After AP-3a, the UFSD JCL proc needs **no DD cards** for disks.

---

## Deliverables

### Item 1: Parmlib Parser (`ufsd#cfg.c`, new)

New module that reads and parses a Parmlib member.

**Config syntax:**

```
/* SYS1.PARMLIB(UFSDPRM0) */

/* Root filesystem — auto-created if missing */
ROOT     DSN(SYS1.UFSD.ROOT) SIZE(1M) BLKSIZE(4096)

/* Mounts */
MOUNT    DSN(HTTPD.WEBROOT)       PATH(/www)        MODE(RO)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/IBMUSER)  MODE(RW) OWNER(IBMUSER)
```

**Parsing rules:**

- Lines starting with `/*` are comments (skip to `*/`)
- Keywords: `ROOT`, `MOUNT`
- Parameters in `KEY(value)` syntax, unquoted
- `DSN` — dataset name (required)
- `PATH` — mount point (required for MOUNT, implicit `/` for ROOT)
- `SIZE` — root disk size, only for ROOT (e.g. `1M`, `500K`)
- `BLKSIZE` — block size, only for ROOT (default 4096)
- `MODE` — `RO` or `RW` (default `RO`)
- `OWNER` — userid for write access (optional)

**Data structure:**

```c
#define UFSD_CFG_MAX_MOUNTS  16

typedef struct ufsd_mount_cfg {
    char     dsname[45];     /* dataset name                */
    char     path[256];      /* mount path                  */
    char     owner[9];       /* owner userid (or empty)     */
    unsigned mode;           /* UFSD_MOUNT_RO / UFSD_MOUNT_RW */
} UFSD_MOUNT_CFG;

typedef struct ufsd_config {
    char             root_dsname[45];
    unsigned         root_size;       /* bytes (from SIZE param)  */
    unsigned         root_blksize;    /* block size (default 4096)*/
    unsigned         nmounts;
    UFSD_MOUNT_CFG   mounts[UFSD_CFG_MAX_MOUNTS];
} UFSD_CONFIG;
```

**Parmlib location:** The STC JCL gets one DD card:

```jcl
//UFSDPRM  DD  DSN=SYS1.PARMLIB(UFSDPRM0),DISP=SHR
```

Read via standard `fopen("DD:UFSDPRM", "r")` + `fgets` line by line.

**Functions:**

```c
int  ufsd_cfg_read(UFSD_CONFIG *cfg);           /* read from DD:UFSDPRM */
void ufsd_cfg_dump(const UFSD_CONFIG *cfg);     /* WTO summary          */
```

### Item 2: DYNALLOC for Disk Datasets (`ufsd#ini.c`, modify)

Replace TIOT-scan + pre-existing DD cards with SVC 99 dynamic allocation.

**Current flow:**

1. Scan TIOT for `UFSDISK0`–`UFSDISK9`
2. Open each found DD via BDAM

**New flow:**

1. `ufsd_cfg_read()` → `UFSD_CONFIG`
2. For each mount in config:
   a. DYNALLOC (`SVC 99`): allocate dataset to generated DD name
   b. Open DD via BDAM (existing `ufsd_disk_open`)
   c. Read boot block + superblock (existing)
   d. Store mount metadata (path, mode, owner) in `UFSD_DISK`

**DYNALLOC on MVS 3.8j:**

```c
/* SVC 99 text units for DSNAME allocation */
struct s99rb {
    unsigned char  s99rblen;    /* length = 20           */
    unsigned char  s99verb;     /* 01 = ALLOC            */
    unsigned short s99flag1;    /* 0                     */
    unsigned short s99error;    /* error reason (output)  */
    unsigned short s99info;     /* info reason (output)   */
    void          *s99txtpp;    /* -> text unit ptr list  */
    /* ... */
};
```

crent370 should have `__svc99()` or `dynalloc()` — check availability.
If not, inline SVC 99 via `__asm__`.

**Generated DD names:** `UFD00001`, `UFD00002`, ... (internal, not visible
to the user). Freed via DYNFREE (SVC 99 verb=2) at shutdown.

### Item 3: Root Disk Auto-Create (`ufsd#ini.c`, modify)

On startup, before processing MOUNT statements:

1. Try DYNALLOC on `cfg->root_dsname`
2. If dataset not found (S99ERROR = 0x1708):
   a. Allocate new BDAM dataset via DYNALLOC with SPACE parameter
   b. Format: write boot block, superblock, inode list, root dir
   c. Create `/etc/` directory on root
   d. Create mount-point directories for all configured mounts
3. Mount root disk as `/`, MODE=RO

**Size calculation:** `cfg->root_size` → number of BDAM blocks.
For 1M at 4096 bytes: 256 blocks.

The format logic can reuse `ufsd_sb_write`, `ufsd_ino_write`,
`ufsd_blk_write` — same functions used at runtime. No need for a
separate FORMAT program for the root disk.

### Item 4: Mount Metadata in UFSD_DISK (`ufsd.h`, modify)

Extend `UFSD_DISK` with mount info:

```c
struct ufsd_disk {
    /* ... existing fields ... */
    char     dsname[45];        /* dataset name (from config)  */
    char     mountpath[256];    /* mount point path            */
    char     mount_owner[9];    /* owner userid (or empty)     */
    unsigned mount_mode;        /* UFSD_MOUNT_RO / UFSD_MOUNT_RW */
    char     ddname[9];         /* generated DD name           */
};

#define UFSD_MOUNT_RO  0x00
#define UFSD_MOUNT_RW  0x01
```

`mountpath` already exists (set by `ufsd_disk_mount`). Add `dsname`,
`mount_owner`, `mount_mode`, `ddname`.

### Item 5: Write Access Check (`ufsd#fil.c`, modify)

New helper function:

```c
static int
ufsd_check_write(UFSD_DISK *disk, UFSD_SESSION *sess)
{
    if (disk->mount_mode == UFSD_MOUNT_RO)
        return UFSD_RC_ROFS;

    if (disk->mount_owner[0] != '\0'
        && strcmp(sess->owner, disk->mount_owner) != 0)
        return UFSD_RC_EACCES;

    return UFSD_RC_OK;
}
```

**New return codes in `ufsd.h`:**

```c
#define UFSD_RC_ROFS    0x44    /* read-only filesystem */
#define UFSD_RC_EACCES  0x48    /* permission denied    */
```

**Call sites (5):**

| Function | When |
|----------|------|
| `do_fopen` | if `mode & UFSD_OPEN_WRITE` |
| `do_fwrite` | before any write |
| `do_mkdir` | before inode alloc |
| `do_rmdir` | before dir removal |
| `do_remove` | before file removal |

Add both RCs to the hard-error list in `ufsd#que.c`? No — `ROFS` and
`EACCES` are soft errors (client can retry on a different mount). Don't
count them as errors.

### Item 6: MODIFY MOUNT Update (`ufsd#cmd.c`, modify)

Update MOUNT command syntax:

```
/F UFSD,MOUNT DSN=HERC02.SITE.WIKI,PATH=/sites/wiki,MODE=RW,OWNER=HERC02
```

Parser extracts `DSN`, `PATH`, `MODE`, `OWNER` from the command string.
Calls `ufsd_disk_mount_dyn()` which does DYNALLOC + open + mount.

Existing `DD=` syntax removed (or kept as fallback for testing).

### Item 7: STATS Update (`ufsd#cmd.c`, modify)

Extend STATS output to show mount info:

```
UFSD018I DISK 0 (UFSDISK0): freeblk=47/2724 freeinode=34/255
```

Becomes:

```
UFSD018I MOUNT /     DSN=SYS1.UFSD.ROOT    RO  freeblk=50/250 freeinode=30/60
UFSD019I MOUNT /www  DSN=HTTPD.WEBROOT      RO  freeblk=47/2724 freeinode=34/255
UFSD019I MOUNT /u/IBMUSER DSN=IBMUSER.UFSHOME RW(IBMUSER) freeblk=200/500 ...
```

---

## Implementation Order

| Step | Item | Description | Effort |
|------|------|-------------|--------|
| 1 | Item 4 | Extend `UFSD_DISK` with mount metadata | Small |
| 2 | Item 1 | Parmlib parser (`ufsd#cfg.c`) | Medium |
| 3 | Item 2 | DYNALLOC in `ufsd#ini.c` | Medium |
| 4 | Item 3 | Root disk auto-create | Medium |
| 5 | Item 5 | Write access check (5 call sites) | Small |
| 6 | Item 6 | MODIFY MOUNT update | Small |
| 7 | Item 7 | STATS update | Small |

**Total estimated effort:** ~800–1000 new/changed lines.

---

## Testing

### Unit Tests

- Parse valid Parmlib with ROOT + 3 MOUNTs → verify `UFSD_CONFIG` fields
- Parse Parmlib with comments, blank lines, missing optional params
- Parse invalid Parmlib → error messages, no crash

### Integration Tests (manual on MVS)

1. **Fresh start:** Delete `SYS1.UFSD.ROOT`, start UFSD → root disk created,
   `/etc/` exists, mount-point dirs exist
2. **Normal start:** Root disk exists → mounted RO, MOUNT statements processed
3. **Write check (RO):** FTP PUT to `/www/test.txt` → rejected (ROFS)
4. **Write check (owner):** FTP as IBMUSER → PUT to `/u/IBMUSER/test.txt` → OK
5. **Write check (wrong owner):** FTP as HERC02 → PUT to `/u/IBMUSER/test.txt` → rejected (EACCES)
6. **Dynamic mount:** `/F UFSD,MOUNT DSN=...,PATH=/test,MODE=RW` → works, visible in STATS
7. **Dynamic unmount:** `/F UFSD,UNMOUNT PATH=/test` → removed from namespace
8. **STATS:** Shows all mounts with DSN, path, mode, owner, free counts

### FTP Test Suite Update

Add T16/T17:

- **T16: RO mount rejection** — PUT to `/www/...` must fail with 553 (permission denied)
- **T17: Owner check** — PUT as wrong user must fail, PUT as correct user must succeed

---

## Files Changed

| File | Change |
|------|--------|
| `include/ufsd.h` | `UFSD_DISK` extended, new RCs, `UFSD_CONFIG` struct |
| `src/ufsd#cfg.c` | **New** — Parmlib parser |
| `src/ufsd#ini.c` | DYNALLOC replaces TIOT scan, root auto-create |
| `src/ufsd#fil.c` | `ufsd_check_write()` + 5 call sites |
| `src/ufsd#cmd.c` | MOUNT syntax, STATS format |
| `src/ufsd.c` | Startup calls `ufsd_cfg_read()` before init |
| `jcl/UFSD.jcl` | Remove UFSDISK* DDs, add UFSDPRM DD |

---

## Open Questions

1. **SVC 99 in crent370:** Does `__svc99()` or similar exist? If not,
   inline asm for SVC 99 needed. Check `clibos.h` or `clibdyn.h`.

2. **BDAM SPACE allocation:** When auto-creating the root disk, we need
   to specify SPACE in the DYNALLOC. Format: `SPACE=(blksize,blocks)`.
   Need to verify SVC 99 text unit for SPACE on MVS 3.8j.

3. **Mount-point auto-creation:** When config says `PATH(/u/IBMUSER)`,
   UFSD needs to create both `/u` and `/u/IBMUSER` on the root disk
   before mounting. `mkdir -p` logic, similar to `MkDirAll` in ufsd-utils.

4. **Parmlib DD or dataset?** Current design uses `DD:UFSDPRM` in JCL.
   Alternative: hardcode `SYS1.PARMLIB(UFSDPRM0)` and DYNALLOC it.
   DD is simpler and more flexible (operator can point to any dataset).
