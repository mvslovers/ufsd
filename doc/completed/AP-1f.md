# AP-1f: Integration + HTTPD Binding — DONE

libufsd client stub library + HTTPD/FTPD integration. Phase 1 + Phase 2 complete.

## What was built
- `client/libufs.c`: ~30 API-compatible stub functions (asm aliases match ufs370 NCALIB members)
- `include/libufs.h`: Drop-in replacement for ufs370 `ufs.h`
- HTTPD patched: `process_httpd_ufs()` uses UFSREQ_SESS_OPEN instead of local `ufs_sys_new()`
- FTPD: gets own UFSD session (no longer shares `httpd->ufssys`)
- MODIFY MOUNT/UNMOUNT commands via `ufsd#ini.c` (`ufsd_disk_mount` / `ufsd_disk_umount`)
- MODIFY TRACE DUMP via `ufsd#trc.c`
- ESTAE recovery in `ufsd.c` (SSCT deregistration on abend)
- diropen/dirread/dirclose implemented in `ufsd#fil.c`
- GETCWD implemented
- 4K FREAD buffer pool path (feature/4k-buf-pool-fread-fwrite branch)
- Read-ahead buffer (4096 bytes) in UFSFILE for ufs_fgetc/ufs_fgets

## Verified
- HTTPD serves static HTML from UFS via UFSD STC
- FTP ls, cd, get (binary) working
- Concurrent HTTPD + FTPD + batch access

## Remaining items carried to AP-2a
- FWRITE 4K path (writes still go through 248-byte inline)
- Timestamps (ctime/mtime/atime never written)
- POST-Bündelung (unconditional POST)
- CS-based buf_alloc/buf_free (for future multi-worker)
- Write-behind buffer for fputc/fputs
