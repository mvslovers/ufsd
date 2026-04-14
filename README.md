# UFSD

**UFSD** is a cross-address-space virtual filesystem daemon for IBM MVS 3.8j, running on Hercules-emulated mainframe systems.

It runs as a Started Task (STC) and gives client programs Unix-like filesystem access to BDAM datasets. Communication happens via IEFSSREQ (SVC 34); the `libufs` client library provides a clean C API and serves as a drop-in replacement for [ufs370](https://github.com/mvslovers/ufs370).

```
Client Address Space          UFSD STC Address Space
─────────────────────         ──────────────────────────────
  your program                  dispatch loop
    │                             ├─ session table (64 slots)
    ▼                             ├─ global file table (256)
  libufs                          ├─ inode / directory / block I/O
    │  IEFSSREQ (SVC 34)          └─ BDAM datasets
    ▼
  SSI router (UFSDSSIR)
    │  CS lock-free queue
    └──── CSA ────────────────▶ UFSD_ANCHOR (request + buffer pools)
```

## Known Limitations

- **No per-file permission system.** Access control is mount-level only (RO/RW mode + OWNER restriction). There are no per-file or per-inode permission checks. Full RACF/RAKF integration is planned for a future release.
- After `/C UFSD`, the ESTAE handler may not fully deregister the SSI — run UFSDCLNP before restarting
- No symbolic links, hard links, or file locking
- Maximum file size: 4.06 MB (single indirect blocks)
- On-disk format is not finalized — images may not be portable across future versions

## Installation

> **Note:** Installation instructions will be finalized once the packaging system is complete. The following is a preliminary guide.

### JCL Procedure

Copy the STC procedure from `samplib/ufsd` to `SYS2.PROCLIB(UFSD)`:

```
//UFSD     EXEC PGM=UFSD,REGION=4M,TIME=1440
//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//UFSDPRM  DD  DSN=SYS2.PARMLIB(UFSDPRM0),DISP=SHR,FREE=CLOSE
```

### Parmlib

Copy `samplib/ufsdprm0` to `SYS2.PARMLIB(UFSDPRM0)` and adjust as needed. A minimal configuration:

```
ROOT     DSN(UFSD.ROOT)
MOUNT    DSN(HTTPD.WEBROOT)      PATH(/www)          MODE(RW)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```

### Disk Setup

Each mounted filesystem lives in a BDAM dataset. Use `ufsd-utils` to create and upload disk images. See [docs/disk-setup.md](docs/disk-setup.md) for the complete workflow.

### Starting and Stopping

```
/S UFSD                         Start with default config
/S UFSD,M=UFSDPRM1              Start with alternate config member
/F UFSD,STATS                   Show status and mount info
/F UFSD,SESSIONS                List active sessions
/F UFSD,HELP                    List available commands
/P UFSD                         Stop the server
```

For the complete parmlib reference and all operator commands, see [docs/configuration.md](docs/configuration.md).

## Configuration

The server is configured through a Parmlib member referenced by the `UFSDPRM` DD card. Key statements:

| Statement | Parameters | Description |
|-----------|-----------|-------------|
| `ROOT` | `DSN` [, `SIZE`, `BLKSIZE`] | Root filesystem (always mounted at `/`, read-only for clients) |
| `MOUNT` | `DSN`, `PATH`, `MODE`, `OWNER` | Additional filesystem mount |

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| `DSN(name)` | dataset name | — | BDAM dataset to mount |
| `PATH(/path)` | absolute path | — | Mount point |
| `MODE(RO\|RW)` | `RO` or `RW` | `RO` | Read-only or read-write |
| `OWNER(userid)` | RACF userid | — | Restrict writes to this userid |

For the complete reference, see [docs/configuration.md](docs/configuration.md).

## Ecosystem

UFSD is part of the [mvslovers](https://github.com/mvslovers) open-source ecosystem for MVS 3.8j.

| Project | Description |
|---------|-------------|
| [crent370](https://github.com/mvslovers/crent370) | C runtime library for MVS 3.8j |
| [ufsd](https://github.com/mvslovers/ufsd) | Cross-address-space UFS filesystem daemon (this project) |
| [ufsd-utils](https://github.com/mvslovers/ufsd-utils) | Host-side CLI for creating and managing UFS disk images |
| [httpd](https://github.com/mvslovers/httpd) | Multi-threaded HTTP/1.1 server |
| [mvsmf](https://github.com/mvslovers/mvsmf) | z/OSMF-compatible REST API (server module for HTTPD) |
| [ftpd](https://github.com/mvslovers/ftpd) | Standalone FTP daemon |

## For Developers

The libufs client library provides a POSIX-like C API for session management, file I/O, directory operations, and metadata queries. Programs that used ufs370 can switch to libufs with no source changes — the function signatures and asm aliases are identical.

Build instructions, architecture overview, the complete libufs API reference (including assembler calling conventions), and the wire protocol are documented in [docs/development.md](docs/development.md).

## Credits

- **Mike Großmann** — author and maintainer of UFSD
- **Michael Dean Rayborn** — original designer of the in-address-space UFS370 filesystem that UFSD is based on

The on-disk format is derived from the Unix V7 filesystem, with code originally contributed to The NetBSD Foundation by UCHIYAMA Yasushi.

## License

See [LICENSE](LICENSE) for details.
