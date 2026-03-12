# AP-1e: File Operations + fd_table — DONE

Full create/write/read/delete cycle working cross-address-space.

## What was built

New modules for on-disk filesystem operations (reimplemented, not using UFS370 library):

| Module | File | Purpose |
|--------|------|---------|
| UFSD#BLK | `src/ufsd#blk.c` | BDAM block read/write (sector-addressed) |
| UFSD#SBL | `src/ufsd#sbl.c` | Superblock I/O, freeblock alloc/free, freeinode alloc/free |
| UFSD#INO | `src/ufsd#ino.c` | Inode I/O + block address resolution (direct blocks only) |
| UFSD#DIR | `src/ufsd#dir.c` | Directory lookup/add/remove, path resolution |
| UFSD#FIL | `src/ufsd#fil.c` | FOPEN/FCLOSE/FREAD/FWRITE/MKDIR/RMDIR/CHGDIR/REMOVE dispatch |
| UFSD#GFT | `src/ufsd#gft.c` | Global File Table (256 entries, STC heap) |

## On-disk format constants

| Constant | Value | Notes |
|----------|-------|-------|
| Root inode | 2 | Inode 1 reserved (BALBLK) |
| Inode size | 128 B | 32 per 4K block |
| Dirent size | 64 B | 64 per 4K block |
| Direct blocks | 16 | addr[0..15], covers 64K per file |

Inode-to-sector: `sector = sb->ilist_start + (ino-1)/32`, `offset = ((ino-1)%32) * 128`

## Verified output
```
UFSTST01I Session opened, token=0x00010001
UFSTST10I MKDIR /test: OK
UFSTST11I FOPEN /test/hello.txt (write): fd=0
UFSTST12I FWRITE 13 bytes: written=13
UFSTST13I FCLOSE write handle: OK
UFSTST14I FOPEN /test/hello.txt (read): fd=0
UFSTST15I FREAD 13 bytes: read=13
UFSTST16I FREAD data: Hello, World!
UFSTST17I FCLOSE read handle: OK
UFSTST18I REMOVE /test/hello.txt: OK
UFSTST19I RMDIR /test: OK
UFSTST02I Session closed
```

## Intentional deferrals (PoC scope)

All deferred to post-AP-1f:
1. Indirect blocks (single/double/triple) — direct blocks cover 64K, sufficient
2. Permission / ACEE checking — no RACF in Phase 1
3. Timestamps (atime/mtime/ctime) — fields present, not filled
4. Superblock freeblock cache refill — pre-populated by mkufs
5. Inode cache / pager layer — direct BDAM I/O, single-client ok
6. diropen/dirread/dirclose — not in test cycle
7. fseek — sequential access only
