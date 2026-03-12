# AP-1f: Integration + HTTPD Binding

**Status:** In progress — libufs, MOUNT/UNMOUNT, TRACE, ESTAE done; HTTPD/FTPD patch pending
**Dependencies:** AP-1e (done)

## Goal

API-compatible client stub library (libufs) that transparently replaces
existing UFS API calls with SSI requests. HTTPD uses the UFSD subsystem
instead of local UFS initialization.

## Scope

- **libufs client stub library:** ~30 stub functions, API-identical to
  existing UFS functions in `ufs370/include/ufs.h`. Each call transparently
  wraps an IEFSSREQ call to the UFSD STC.

- **HTTPD modification:** In `httpconf.c/process_httpd_ufs()`: replace
  `ufs_sys_new()` + `ufsnew()` with `UFSREQ_SESS_OPEN` to UFSD STC.
  The rest (`ufs_fopen`/`ufs_fread`/`ufs_fclose` in `httpopen.c`/`httpfile.c`)
  works transparently through the stub library.

- **FTPD modification:** The FTPD currently shares `httpd->ufssys`. With the
  STC it gets its own session.

- **MODIFY MOUNT/UNMOUNT:** Dynamic mount/unmount at runtime.

- **MODIFY TRACE DUMP:** Output last N trace entries to console.

- **ESTAE exit:** Ensure SSCT deregistration on any STC abend (open item from AP-1c).

## Deliverables

| Artifact          | Description                                            | Status  |
|-------------------|--------------------------------------------------------|---------|
| `libufs (NCALIB)` | Client stub library with ~30 API-compatible functions  | done    |
| `HTTPD Patch`     | Minimal patch for httpconf.c: UFS init via UFSD session| pending |
| MOUNT/UNMOUNT in `ufsd#cmd.c` + `ufsd#ini.c` | MODIFY MOUNT/UNMOUNT command handler | done |
| ESTAE in `ufsd.c` | Abend recovery ensuring SSCT cleanup                   | done (AP-1c) |

## UFS API Functions to Stub

From `ufs370/include/ufs.h` — these are the functions HTTPD/FTPD actually call:

### Session lifecycle
- `ufsnew()` → UFSREQ_SESS_OPEN
- `ufsfree()` → UFSREQ_SESS_CLOSE
- `ufs_signon()` / `ufs_signoff()` → UFSREQ_SIGNON / UFSREQ_SIGNOFF

### File operations
- `ufs_fopen()` → UFSREQ_FOPEN
- `ufs_fclose()` → UFSREQ_FCLOSE
- `ufs_fread()` → UFSREQ_FREAD (chunked for >256 bytes)
- `ufs_fwrite()` → UFSREQ_FWRITE (chunked for >256 bytes)
- `ufs_fgets()` → UFSREQ_FGETS
- `ufs_fputc()` → UFSREQ_FPUTC
- `ufs_fputs()` → UFSREQ_FPUTS
- `ufs_fgetc()` → UFSREQ_FGETC
- `ufs_fseek()` → UFSREQ_FSEEK
- `ufs_feof()` → UFSREQ_FEOF
- `ufs_ferror()` → UFSREQ_FERROR
- `ufs_fsync()` → UFSREQ_FSYNC

### Directory operations
- `ufs_mkdir()` → UFSREQ_MKDIR
- `ufs_rmdir()` → UFSREQ_RMDIR
- `ufs_chgdir()` → UFSREQ_CHGDIR
- `ufs_remove()` → UFSREQ_REMOVE
- `ufs_diropen()` → UFSREQ_DIROPEN
- `ufs_dirread()` → UFSREQ_DIRREAD
- `ufs_dirclose()` → UFSREQ_DIRCLOSE

### Query
- `ufs_get_cwd()` → UFSREQ_GETCWD
- `ufs_get_sys()` → local (returns stub pointer)
- `ufs_set_sys()` → local
- `ufs_get_acee()` / `ufs_set_acee()` → local or UFSREQ_SETACEE

## HTTPD Integration Points

### httpconf.c — `process_httpd_ufs()`

Current code (lines 534–575):
```c
httpd->ufssys = ufs_sys_new();     /* LOCAL — must change */
/* ... disk logging ... */
httpd->ufs = ufs = ufsnew();       /* LOCAL — must change */
```

Replace with:
```c
httpd->ufs = ufs = ufsnew();       /* libufs stub: SESS_OPEN to UFSD */
/* ufssys not needed — STC owns it */
```

### httpopen.c — `http_open()`

Current code (line 97):
```c
if (httpc->ufs) {
    httpc->ufp = ufs_fopen(httpc->ufs, buf, mode);
```

No change needed — `ufs_fopen` is replaced by the libufs stub transparently.

### httpfile.c — `http_send_file()`

Current code (line 60):
```c
len = ufs_fread(&httpc->buf[httpc->len], 1, avail, httpc->ufp);
```

No change needed — `ufs_fread` is replaced by the libufs stub.

### httpconf.c — FTP integration (line 460)

Current:
```c
if (httpd->ufssys) {
    ftpd->sys = httpd->ufssys;
}
```

Replace: FTPD opens its own UFSD session.

## Test Criteria

```
/* Both STCs running */
/S UFSD
/S HTTPD

/* HTTPD opens session to UFSD STC */
HTTPD046I UFS session opened via UFSD subsystem

/* Browser access to static UFS file */
http://mvs:8080/index.html  → 200 OK

/* FTP access to UFS file */
ftp mvs 8021 → ls / → directory listing

/* Dynamic mount */
/F UFSD,MOUNT DD=UFSDISK2,PATH=/extra
UFSD060I Mounted UFSDISK2 on /extra

/* Concurrent access */
HTTPD + FTPD + batch job UFSDTEST all in parallel
→ No errors, STATS shows 3 sessions
```

## Open Items from Previous APs

1. ~~**ESTAE exit** (from AP-1c)~~ — done in commit `e26d170`
2. **APF auth in clients** (from AP-1d): libufs stubs require the caller to be APF-authorized before calling any function. IEFSSREQ (SVC 34) itself does not require APF, but the SSOB function code and SSI router do. The HTTPD STC runs AC(1) and satisfies this.
