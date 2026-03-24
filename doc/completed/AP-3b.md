# AP-3b: Post-AP-3a Cleanup and Quick-Fixes

**Status:** Open  
**Predecessor:** AP-3a (merged, commit c6fa904)  
**Reference:** `doc/ufsd-code-review-c6fa904.md` (code review, same repo)  
**Goal:** Fix all known bugs and cosmetic issues before Phase 4 (Multi-Worker).

---

## Items

### 1. Permission bits hardcoded in libufs DIRREAD (Bug — Code Review #8)

**File:** `client/libufs.c`, function `dirread_one`, lines 422–432  
**Problem:** The `attr[]` string is hardcoded to `rwxr-xr-x` for every file. The actual `mode` bits from the inode are ignored.  
**Fix:** Read the 9 low bits from `mode` and build the permission string:

```c
{
    const char *perms = "rwxrwxrwx";
    int k;
    out->attr[0] = ((mode & 0xF000U) == 0x4000U) ? 'd' : '-';
    for (k = 0; k < 9; k++)
        out->attr[1 + k] = (mode & (1 << (8 - k))) ? perms[k] : '-';
    out->attr[10] = '\0';
}
```

**Test:** Upload a file, check FTP `ls -l` — should show `-rwxr-xr-x` (0755) or whatever the actual mode is, not always `rwxr-xr-x`.

---

### 2. ASID not populated in sessions (Code Review #7)

**File:** `src/ufsd#ssi.c`, line 235  
**Problem:** `req->client_asid = 0` is hardcoded. `/F UFSD,SESSIONS` always shows `ASID=0000`.  
**Fix:** In the SSI router, extract the ASID from the current ASCB. We are in supervisor state (key 0) at this point, so ASCB access is permitted:

```c
{
    void *ascb = req->client_ascb;  /* already captured via __ascb(0) */
    if (ascb)
        req->client_asid = (unsigned)(*(unsigned short *)((char *)ascb + 0x24));
}
```

`ASCBASID` is at offset `0x24` (decimal 36) in the ASCB. Verify against the MVS 3.8j Data Areas manual (ASCB mapping, field ASCBASID).

**Test:** `/F UFSD,SESSIONS` should show non-zero ASID values for each active session.

---

### 3. Outdated comments (Code Review #1, #10)

**File:** `src/ufsd#fil.c`, line 33  
**Fix:** Change `UFSD_DIRREAD_RLEN (72 bytes)` to `UFSD_DIRREAD_RLEN (98 bytes)`.

**File:** `src/ufsd#que.c`, line 84  
**Fix:** Change the comment from "post client ECB from problem state (SVC 2 must not be issued in supervisor state on MVS 3.8j)" to "post client ECB via __xmpost (CVT0PT01) from supervisor state".

---

### 4. MODIFY MOUNT path case handling (Code Review #11)

**File:** `src/ufsd#cmd.c`, function `ufsd_process_cib`, line 41  
**Problem:** The entire CIB buffer is uppercased. This is correct for DSN, MODE, OWNER (MVS convention) but wrong for PATH (Unix convention, case-sensitive).  
**Fix:** Only uppercase the buffer up to and including the command keyword. After identifying `MOUNT`, parse the arguments without uppercasing PATH values. Alternatively: uppercase DSN/MODE/OWNER values individually, leave PATH as-is.  
**Simpler alternative:** Document that all operator-entered paths are uppercased, and that parmlib paths are case-preserved. This matches the MVS operator convention where console input is always uppercase.

---

### 5. `mkdir_p` owner string cleanup (Code Review #4)

**File:** `src/ufsd#ini.c`, lines 318 and 371  
**Fix:** Replace `memcpy(dino.owner, "UFSD    ", 9)` with `strcpy(dino.owner, "UFSD")`. Same for `dino.group`. The struct is already zeroed by `memset(&dino, 0, ...)` so trailing NULs are guaranteed. Same pattern on both create and retrofit paths.

---

### 6. `ufs_sys_term()` memory leak (Code Review #9)

**File:** `client/libufs.c`, line 172  
**Problem:** `ufs_sys_new()` does `calloc` but `ufs_sys_term()` is a no-op. The UFSSYS block is never freed.  
**Fix:** Change `ufs_sys_term` signature and implementation. The asm alias must stay the same:

```c
void ufs_sys_term(UFSSYS **sys)  asm("UFSSYTRM");

void ufs_sys_term(UFSSYS **sys)
{
    if (sys && *sys) {
        free(*sys);
        *sys = NULL;
    }
}
```

Update `include/libufs.h` declaration to match. Check all callers in HTTPD — `ufs_sys_term()` is called in `httpd.c:325`. The caller must pass the address of the pointer.

**IMPORTANT:** This changes the public API signature. Verify that HTTPD and any other callers are updated. If the signature change is too risky, add a new function `ufs_sys_free(UFSSYS **sys)` instead and leave `ufs_sys_term` as the deprecated no-op.

---

### 7. Concept update to K7

**File:** `doc/concept.md`  
**Changes:**
- Update title to "Concept #7"
- Phase 3 status: ✅ Done
- Add Changes from Concept #6 table with AP-3a deliverables: Parmlib parser, DYNALLOC mounts, root-disk RO, mount traversal, ufsd_check_write, ufs_setuser, SYNAD exit, superblock validation, S99 error decoding
- Add AP-3b deliverables to changes table
- Update §12 Known Limitations if any items were resolved
- Update §14 Phase Plan: Phase 3 = Done, Phase 4 = Next

---

## Out of Scope for AP-3b

The following items are noted but intentionally deferred:

| Item | Reason | Target |
|------|--------|--------|
| Root-disk auto-create via DYNALLOC + FORMAT | Requires embedding FORMAT logic in UFSD or calling FORMAT as subprocess. Significant effort. | AP-4 or standalone |
| Freeblock cache overflow (chain-block write-back) | Concept §12.1 #1. Needs inverse chain_refill. Medium effort. | Phase 4 |
| Freeinode cache overflow | Concept §12.1 #2. Same pattern. | Phase 4 |
| FTP Test Suite v5 | New tests for RO-mount rejection, owner-check, setuser. Depends on stable AP-3b. | After AP-3b merge |
| ASID-scan cleanup | Periodic scan for orphaned sessions. Needs ASVT walk. | Phase 4 (Multi-Worker) |
| `do_dirread` / `dir_is_empty` indirect block support | Code Review #2. Directories > 1024 entries. Unrealistic for current usage. | Phase 5 |
| `s_ddn_seq` collision after abend | Code Review #3. DYNALLOC returns error, handled gracefully. | Nice-to-have |

---

## Estimated Effort

Items 1–6 are all small fixes (5–20 lines each). Item 7 (K7 concept) is documentation. Total: ~2 hours agent time.

## Test Criteria

- [ ] FTP `ls -l` shows correct permission bits (not always `rwxr-xr-x`)
- [ ] `/F UFSD,SESSIONS` shows non-zero ASID for active sessions
- [ ] No outdated comments in ufsd#fil.c and ufsd#que.c
- [ ] `mkdir_p` uses `strcpy` for owner/group
- [ ] Build clean, no new warnings
- [ ] Existing FTP test suite v4: 44/46 PASS, 0 FAIL (unchanged)
