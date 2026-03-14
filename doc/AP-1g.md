# AP-1g: Timestamp + Owner/Group Bugfixes

**Status:** Not started  
**Dependencies:** AP-1f (done), feature/4k-buf-pool-fread-fwrite merged  
**Found by:** FTP Test Suite v3 (ufsd-ftp-test.sh)

## Goal

Fix the two remaining data fidelity bugs found during FTP integration testing.
After AP-1g, the UFSD LIST output must be identical to the ufs370 reference
(except for mountpoint indicators, which are cosmetic).

---

## Bug 1: Timestamps show 1970 instead of current date

### Symptom

FTP LIST shows `1970 Feb 07 01:34` for files written by UFSD.
Freshly created files/dirs also show 1970 dates.
The value (~3.2M seconds) matches Hercules uptime, not wallclock time.

### Root cause

`time()` on MVS 3.8j (crent370) returns seconds since IPL, not since
Unix epoch — unless the TOD clock is correctly set.  Even if the TOD
clock is set, `time()` returns a 32-bit `time_t` which may not match
what ufs370 expects.

ufs370 uses the **time64** package from crent370 (`time64.h`, `clib64.h`)
which provides `mtime64_t` (64-bit milliseconds since epoch) and
`mlocaltime64()` for correct formatting.  libufs.h already includes
these headers.

### Fix

Use `__mtime64()` from crent370's time64 package instead of `time()`.
The on-disk inode has two 32-bit fields per timestamp:

```c
unsigned ctime_sec;    /* offset 0x08 */
unsigned ctime_usec;   /* offset 0x0C */
unsigned mtime_sec;    /* offset 0x10 */
unsigned mtime_usec;   /* offset 0x14 */
unsigned atime_sec;    /* offset 0x18 */
unsigned atime_usec;   /* offset 0x1C */
```

The time64 value (milliseconds) must be split into sec + usec:

```c
#include "time64.h"
#include "clib64.h"

mtime64_t now = __mtime64();             /* ms since epoch, 64-bit */
unsigned  sec  = __64_to_u32(__64_div_u32(now, 1000));
unsigned  usec = __64_to_u32(__64_mod_u32(now, 1000)) * 1000;

dino.ctime_sec  = sec;
dino.ctime_usec = usec;
dino.mtime_sec  = sec;
dino.mtime_usec = usec;
dino.atime_sec  = sec;
dino.atime_usec = usec;
```

Note: `__64_div_u32` and `__64_mod_u32` are from clib64.h.  If these
are not available, check what ufs370 itself uses — it already solves
this exact problem.

### Affected files

| File | Change |
|------|--------|
| `src/ufsd#fil.c` — `do_mkdir` | Set ctime/mtime/atime on new dir inode |
| `src/ufsd#fil.c` — `do_fopen` | Set ctime/mtime/atime when creating file (OPEN_WRITE on new) |
| `src/ufsd#fil.c` — `do_fwrite` | Update mtime after successful write |
| `include/ufsd.h` or Makefile | May need to add time64.h / clib64.h include path |

### Verification

```
FTP test suite T5:
  ✓ No 1970 timestamps in existing files
  ✓ Fresh file shows current year
FTP test suite T11:
  ✓ Fresh dir has current year
```

---

## Bug 2: Owner/Group missing in LIST output

### Symptom

Reference (ufs370):
```
drwxr-xr-x   2 HERC01   ADMIN       256 2019 Feb 25 14:17 css
```

UFSD:
```
drwxr-xr-x   2                      256 2019 Feb 25 14:17 css
```

Owner and group fields are empty.

### Root cause

`do_dirread` in `ufsd#fil.c` reads the inode via `ufsd_ino_read` but
does not copy `edino.owner` and `edino.group` into the response data.

Current DIRREAD response layout (`UFSD_DIRREAD_RLEN = 76`):

```
[0..3]   = ino (unsigned)
[4..7]   = filesize (unsigned)
[8..9]   = mode (unsigned short)
[10..11] = nlink (unsigned short)
[12..71] = name (60 bytes, NUL-terminated)
[72..75] = mtime_sec (unsigned)
```

Missing: owner (9 bytes) and group (9 bytes).

### Fix

Extend the DIRREAD response layout to include owner and group:

```
[76..84] = owner (9 bytes, NUL-terminated)
[85..93] = group (9 bytes, NUL-terminated)
New total: 94 bytes
```

#### ufsd.h

```c
#define UFSD_DIRREAD_RLEN   94U   /* was 76 */
```

#### ufsd#fil.c — do_dirread (around line 917)

After the existing `edino` field copies, add:

```c
memcpy(resp_data + 76, edino.owner, 9);
memcpy(resp_data + 85, edino.group, 9);
```

#### client/libufs.c — ufs_dirread

In the response parsing, populate the UFSDLIST fields:

```c
memcpy(ddesc->result.owner, ufsssob.data + 76, 9);
ddesc->result.owner[8] = '\0';
memcpy(ddesc->result.group, ufsssob.data + 85, 9);
ddesc->result.group[8] = '\0';
```

#### include/libufs.h — UFSDLIST (already has the fields)

```c
char owner[9];   /* already present */
char group[9];   /* already present */
```

No change needed — the struct has the fields, they just aren't populated.

### Verification

```
FTP test suite T2 (visual check):
  LIST / should show "HERC01   ADMIN" (or whatever owner/group
  is set in the inode) instead of blank columns.
```

---

## Implementation Order

1. **Timestamps first** — higher visibility, affects more tests
2. **Owner/Group second** — straightforward field copy

Both are small, independently committable changes.

## Acceptance Criteria

Run FTP Test Suite v3 against UFSD.  All of these must pass:

```
T2:  LIST shows owner/group columns populated
T5:  No 1970 timestamps, fresh file shows current year
T6:  GET ASCII still byte-identical (regression)
T9:  PUT+GET round-trip still works (regression)
T11: Fresh dir shows current year
```
