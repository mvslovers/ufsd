# AP-2a: Post-PoC Hardening

**Status:** Not started  
**Dependencies:** AP-1f (done), feature/4k-buf-pool-fread-fwrite (merge first)  
**Prerequisite:** Merge the 4K FREAD branch to main before starting.

## Goal

Harden the UFSD filesystem server for real-world HTTPD/FTPD operation.
Bundle of six focused fixes that each address a concrete gap found during
AP-1f integration testing.

## Scope Overview

| # | Item | Priority | Effort | Impact |
|---|------|----------|--------|--------|
| 1 | FWRITE 4K path | High | Medium | FTP uploads, any write >248B |
| 2 | Timestamps (ctime/mtime/atime) | High | Small | FTP LIST shows 1970, dirread mtime=0 |
| 3 | POST-Bündelung (conditional POST) | Medium | Small | SVC overhead under multi-client load |
| 4 | CS-based buf_alloc/buf_free | Medium | Small | Prep for Phase 3 multi-worker |
| 5 | FWRITE 4K client path (libufs) | High | Medium | Completes write-side 4K story |
| 6 | Write-behind buffer in libufs | Medium | Medium | fputc/fputs per-char overhead |

---

## Item 1: FWRITE 4K Path (Server Side)

**Problem:** `do_fwrite` reads data from `req->data + 8` (inline, max 248 bytes).
`LIBUFS_WRITE_CHUNK` is capped at 248. FTP uploads and any write >248 bytes
require multiple SSI round-trips per 4K block.

**Fix:** Mirror the FREAD 4K pattern:

### libufs.c (client side)
When `want > LIBUFS_WRITE_INLINE`:
- Set `ufsssob.buf_ptr = src + done` (source data in client address space)
- Set `ufsssob.buf_len = want` (up to 4096)
- SSI router copies from client buffer to CSA UFSBUF in key-0 window

### ufsd#ssi.c (SSI router)
In Phase 1 (key-0 window), after filling req fields:
```
if (ufsssob->buf_ptr != NULL && req->func == UFSREQ_FWRITE) {
    req->buf = ufsd_buf_alloc(anchor);
    if (req->buf) {
        unsigned n = ufsssob->buf_len;
        if (n > 4096) n = 4096;
        memcpy(req->buf->data, ufsssob->buf_ptr, n);
        /* store actual count in req->data[4..7] */
        *(unsigned *)(req->data + 4) = n;
    }
}
```

### ufsd#fil.c (do_fwrite)
When `req->buf != NULL`:
- Read data from `req->buf->data` instead of `req->data + 8`
- Count from `*(unsigned *)(req->data + 4)`
- Do NOT free buf — `ufsd_dispatch` does that

### ufsd#que.c (ufsd_dispatch)
After dispatch returns, in key-0 window:
- If `req->buf` is set and func was FWRITE, free the buffer:
  `ufsd_buf_free(anchor, req->buf); req->buf = NULL;`

### libufs.c
- Change `LIBUFS_WRITE_CHUNK` from 248 to 4096
- Write loop: when `want > LIBUFS_WRITE_INLINE`, set `buf_ptr`/`buf_len`

### Test
UFSDTEST: write 8192 bytes, verify round-trips reduced from ~33 to 2.

---

## Item 2: Timestamps

**Problem:** `ctime_sec`, `mtime_sec`, `atime_sec` are never written. All
files and directories show epoch zero. FTP LIST displays `Jan  1  1970`.

### Design decision: UFSD_DINODE timestamp format

ufs370 stores timestamps as `UFSTIMEV` — a union of two formats:
- **v1** (legacy): `{seconds, useconds}` — two UINT32. Detected by `useconds < 1000000`.
- **v2** (current): `mtime64_t` (__64) — 8 bytes, milliseconds since epoch.

ufs370 writes v2 exclusively (`dinode->ctime.v2 = mtime64(NULL)` in
ufs#crte.c, ufs#md.c, ufs#open.c). Pre-existing v1 inodes are detected
by the useconds heuristic and converted on read (ufs#dopn.c).

**Current UFSD_DINODE** defines timestamps as separate `unsigned` fields
(`ctime_sec`, `ctime_usec`). This is byte-identical to UFSTIMEV in memory
but semantically wrong: when ufs370 writes a v2 value, UFSD reads the
high word as "seconds" and the low word as "microseconds" — both are
meaningless fragments of a millisecond counter.

**Decision:** Refactor UFSD_DINODE to use `UFSTIMEV` (from ufs370) instead
of the split unsigned fields. This gives:
- Correct semantics — field names say what is inside
- One canonical on-disk inode type shared with ufs370
- Automatic v1/v2 detection for pre-existing inodes
- No silent interpretation bugs in future code

The alternative (pragmatic `memcpy` over the two unsigned fields, keeping
the struct unchanged) was rejected because it creates a trap: any code
reading `dino.ctime_sec` would silently get the wrong value.

### Refactor scope

1. **`include/ufsd.h`** — add `#include <time64.h>`, replace the six
   `unsigned` timestamp fields in `UFSD_DINODE` with three `UFSTIMEV`
   fields (`ctime`, `mtime`, `atime`). Struct size stays 128 bytes.

2. **`src/ufsd#fil.c`** — update all references:
   - `do_mkdir`, `do_fopen` (create): `dino.ctime.v2 = mtime64(NULL);`
     `dino.mtime = dino.atime = dino.ctime;`
   - `do_fwrite` (after write): `dino.mtime.v2 = mtime64(NULL);`
   - `do_dirread` (line 922): read `edino.mtime` as UFSTIMEV, convert
     to seconds for the wire format using v1/v2 detection.

3. **No other files affected** — ctime_sec/ctime_usec are not referenced
   outside ufsd#fil.c and ufsd.h.

| Operation | Fields to set |
|-----------|---------------|
| `do_mkdir` (create dir inode) | ctime, mtime, atime = mtime64(NULL) |
| `do_fopen` with OPEN_WRITE (create file inode) | ctime, mtime, atime = mtime64(NULL) |
| `do_fwrite` (after write completes) | mtime = mtime64(NULL) |
| `do_remove` / `do_rmdir` | (none — inode is freed) |

### do_dirread: v1/v2 conversion for wire format

The DIRREAD response sends mtime as a 32-bit seconds value (wire offset 72).
The conversion must handle both timestamp formats on disk:

```c
unsigned mtime_sec = 0;
if (edino.mtime.v1.useconds < 1000000U) {
    /* v1: seconds field is real seconds */
    mtime_sec = edino.mtime.v1.seconds;
} else {
    /* v2: mtime64 milliseconds — divide by 1000 to get seconds */
    __64 tmp;
    tmp = edino.mtime.v2;
    __64_div_u32(&tmp, 1000U, &tmp);
    mtime_sec = __64_to_u32(&tmp);
}
*(unsigned *)(resp_data + 72) = mtime_sec;
```

### Dependencies

- `time64.h` and `clib64.h` from crent370 (already a link dependency)
- `mtime64` / `__64_div_u32` / `__64_to_u32` exports from crent370 NCALIB
  (TM64MTIM, @@64DU32, @@64TU32 — already present)

### Test
After mkdir + write, UFSDTEST reads inode via dirread and checks mtime > 0.
FTP LIST shows current date instead of 1970.
Pre-existing v1 inodes display correctly (v1/v2 auto-detection).

---

## Item 3: POST-Bündelung (Conditional POST)

**Problem:** `ufsd#ssi.c` line 254 calls `__xmpost` unconditionally. Under
load with N simultaneous clients, N POST SVCs fire even though the STC
only needs 1 wake-up to drain the queue.

**Fix:** `queue_append` returns whether the queue was previously empty.
`__xmpost` only fires in that case.

### ufsd#ssi.c — queue_append
```c
/* Returns 1 if queue was empty before this append, 0 otherwise */
static int
queue_append(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    int was_empty;
    req->next = NULL;
    cs_lock_acquire(anchor);
    was_empty = (anchor->req_head == NULL);
    if (anchor->req_tail) {
        anchor->req_tail->next = req;
    } else {
        anchor->req_head = req;
    }
    anchor->req_tail = req;
    cs_lock_release(anchor);
    return was_empty;
}
```

### ufsd#ssi.c — caller
```c
int was_empty = queue_append(anchor, req);
if (was_empty)
    __xmpost(anchor->server_ascb, &anchor->server_ecb, 0);
else
    __uinc(&anchor->stat_posts_saved);
```

### ufsd#cmd.c — STATS output
Add: `wtof("UFSD017I POSTS SAVED:    %8u", anchor->stat_posts_saved);`

### Test
Run 3 concurrent UFSDTEST instances. Check `stat_posts_saved > 0` via `/F UFSD,STATS`.

---

## Item 4: CS-Based buf_alloc / buf_free

**Problem:** `ufsd#buf.c` uses plain load/store for `anchor->buf_free` chain.
Currently safe (single-threaded STC, SSI router serialised by req_lock),
but will race under Phase 3 multi-worker.

**Fix:** Apply same CS-pop/CS-push pattern as the request pool in `ufsd#ssi.c`.

**IMPORTANT:** These functions must NOT contain their own `__super`/`__prob`.
Both call sites (ufsd_dispatch and ufsdssir) are already inside key-0 blocks.
Nesting `__super`/`__prob` drops the caller to problem state on return,
causing 0C2 (privileged operation) or 0C4 (protection exception) in the
subsequent supervisor-state code. This was the root cause of the 0C2 abend
fixed in commit `4e29ad5`.

### ufsd#buf.c — buf_alloc
```c
/* Must be called from supervisor state (PSW key 0). */
UFSBUF *
ufsd_buf_alloc(UFSD_ANCHOR *anchor)
{
    UFSBUF   *buf;
    unsigned  old, nxt;

    for (;;) {
        buf = anchor->buf_free;
        if (!buf) break;
        old = (unsigned)buf;
        nxt = (unsigned)buf->next;
        __asm__ __volatile__(
            "CS %0,%2,%1"
            : "+r"(old), "+m"(anchor->buf_free)
            : "r"(nxt)
            : "cc");
        if (old == (unsigned)buf) {
            buf->next = NULL;
            __udec(&anchor->buf_count);
            break;
        }
    }

    return buf;
}
```

### ufsd#buf.c — buf_free
```c
/* Must be called from supervisor state (PSW key 0). */
void
ufsd_buf_free(UFSD_ANCHOR *anchor, UFSBUF *buf)
{
    unsigned old;

    if (!buf) return;

    for (;;) {
        buf->next = anchor->buf_free;
        old = (unsigned)buf->next;
        __asm__ __volatile__(
            "CS %0,%2,%1"
            : "+r"(old), "+m"(anchor->buf_free)
            : "r"((unsigned)buf)
            : "cc");
        if (old == (unsigned)buf->next) {
            __uinc(&anchor->buf_count);
            break;
        }
    }
}
```

### Test
Functional: existing UFSDTEST passes. Structural: code review confirms CS pattern matches request pool.

---

## Item 5: FWRITE 4K Client Path (libufs)

Covered by Item 1 (client-side changes in libufs.c). Listed separately for
tracking. `LIBUFS_WRITE_CHUNK` changes from 248 to 4096.

---

## Item 6: Write-Behind Buffer in libufs

**Problem:** `ufs_fputc` and `ufs_fputs` issue one SSI call per character/string.
Same problem that read-ahead solved for `ufs_fgetc`.

**Fix:** Add `wbuf[4096]` / `wbuf_len` to UFSFILE. `ufs_fputc` writes into
`wbuf`, flushes via `ufs_fwrite` when full. `ufs_fclose` and `ufs_fsync`
flush before closing.

### libufs.h
```c
#define LIBUFS_PUTC_BUFSZ  4096
struct libufs_file {
    /* ... existing fields ... */
    unsigned wbuf_len;
    char     wbuf[LIBUFS_PUTC_BUFSZ];
};
```

### libufs.c
```c
static int libufs_wbuf_flush(UFSFILE *fp) {
    if (fp->wbuf_len == 0) return 0;
    UINT32 n = ufs_fwrite(fp->wbuf, 1, fp->wbuf_len, fp);
    fp->wbuf_len = 0;
    return (n > 0) ? 0 : -1;
}

INT32 ufs_fputc(INT32 c, UFSFILE *file) {
    if (!file || file->fd < 0) return UFS_EOF;
    file->wbuf[file->wbuf_len++] = (char)c;
    if (file->wbuf_len >= LIBUFS_PUTC_BUFSZ)
        libufs_wbuf_flush(file);
    return c;
}
```

Update `ufs_fclose`: call `libufs_wbuf_flush(fp)` before FCLOSE request.
Update `ufs_fsync`: call `libufs_wbuf_flush(fp)`.

### Note on UFSFILE size
With rbuf[4096] + wbuf[4096], each UFSFILE grows to ~8.2K. HTTPD can have
multiple files open simultaneously (worker threads). At 9 workers × 1 file
each = ~74K heap. Acceptable. Document in CLAUDE.md.

### Test
UFSDTEST: fputc loop writing 1000 chars. Verify only 1 SSI call instead of 1000.

---

## Implementation Order

Recommended sequence (each item is independently committable):

```
1. Merge feature/4k-buf-pool-fread-fwrite to main
2. Item 2: Timestamps (smallest, most visible fix)
3. Item 3: POST-Bündelung (small, independent)
4. Item 4: CS-based buf pool (prep, no functional change)
5. Item 1+5: FWRITE 4K path (server + client together)
6. Item 6: Write-behind buffer (depends on FWRITE 4K)
```

## Acceptance Criteria

All of the following must pass on Hercules:

```
1. UFSDTEST full cycle (mkdir/write/read/verify/delete) — OK
2. FTP LIST shows current timestamps (not 1970) — OK
3. FTP binary GET of >4K file — OK (was already working)
4. FTP binary PUT of >4K file — OK (new: FWRITE 4K path)
5. /F UFSD,STATS shows stat_posts_saved > 0 under load — OK
6. HTTPD serves static HTML from UFS — OK (regression check)
```
