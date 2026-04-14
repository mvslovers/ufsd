# UFSD Configuration Reference

UFSD is configured through a Parmlib member, referenced by the `UFSDPRM` DD card in the STC JCL procedure.

## Format

```
/* Lines between slash-star are comments */

ROOT     DSN(UFSD.ROOT)
MOUNT    DSN(HTTPD.WEBROOT)      PATH(/www)          MODE(RW)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
```

Comments are delimited by `/*` and `*/` (block comments, like JCL). Blank lines are ignored. Statements can span one line only.

## ROOT Statement

Defines the root filesystem, always mounted at `/`.

```
ROOT     DSN(dataset.name) [SIZE(n)] [BLKSIZE(n)]
```

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| `DSN(name)` | dataset name | — | **Required.** BDAM dataset for the root filesystem. |
| `SIZE(n)` | `500K`, `1M`, etc. | — | Root disk size (used for auto-creation in a future release). |
| `BLKSIZE(n)` | 512–8192 | `4096` | Block size in bytes. |

The root filesystem is always read-only for clients. UFSD temporarily sets it read-write during startup to create mount-point directories, then switches it to read-only before accepting client requests.

A size of 1 MB is sufficient for the root (it only holds mount-point directories).

## MOUNT Statement

Defines an additional filesystem mount.

```
MOUNT    DSN(dataset.name) PATH(/mount/point) [MODE(RO|RW)] [OWNER(userid)]
```

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| `DSN(name)` | dataset name | — | **Required.** BDAM dataset to mount. |
| `PATH(/path)` | absolute path | — | **Required.** Mount point in the unified directory tree. |
| `MODE(RO\|RW)` | `RO` or `RW` | `RO` | Read-only or read-write. |
| `OWNER(userid)` | RACF userid | — | Restrict writes to this userid only. |

## Access Control

UFSD enforces access control at three levels, applied in order:

**Level 1 — Mount mode (RO/RW).** Per-disk flag from Parmlib or dynamic MOUNT. Read-only mounts reject all write operations (FOPEN write, FWRITE, MKDIR, RMDIR, REMOVE) with `UFSD_RC_ROFS`.

**Level 2 — Owner check.** If the mount has an OWNER, only sessions whose userid matches the OWNER may write. Other sessions get `UFSD_RC_EACCES`. Mounts without OWNER allow any authenticated user to write (if MODE=RW).

**Level 3 — RACF/RAKF per-file permissions.** Not implemented. There are no per-file or per-inode permission checks. The `mode` bits stored in inodes are informational only and not enforced by UFSD. Full RACF/RAKF integration with per-operation RACHECK is planned for a future release.

The check is applied in `do_fopen` (write mode), `do_mkdir`, `do_rmdir`, `do_remove`, and `do_fwrite` — five call sites, one helper function:

```c
int ufsd_check_write(UFSD_DISK *disk, UFSD_SESSION *sess);
/* Returns UFSD_RC_OK, UFSD_RC_ROFS, or UFSD_RC_EACCES */
```

## Mount Namespace

Each BDAM dataset is mounted on a path in a unified directory tree:

```
/                        UFSD.ROOT              RO   (root filesystem)
/www                     HTTPD.WEBROOT          RW   (web content)
/u/ibmuser               IBMUSER.UFSHOME        RW   OWNER(IBMUSER)
/u/mvsce01               MVSCE01.UFSHOME        RW   OWNER(MVSCE01)
/tmp                     UFSD.SCRATCH           RW   (shared scratch)
```

Mount points must exist as directories on the parent filesystem. UFSD creates them automatically when processing MOUNT statements at startup.

## Example Configurations

### Minimal (root only)

```
ROOT     DSN(UFSD.ROOT)
```

### Web Server

```
ROOT     DSN(UFSD.ROOT)
MOUNT    DSN(HTTPD.WEBROOT)      PATH(/www)          MODE(RW)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```

### Multi-User

```
ROOT     DSN(UFSD.ROOT)
MOUNT    DSN(HTTPD.WEBROOT)      PATH(/www)          MODE(RW)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(MVSCE01.UFSHOME)    PATH(/u/mvsce01)    MODE(RW) OWNER(MVSCE01)
MOUNT    DSN(MVSCE02.UFSHOME)    PATH(/u/mvsce02)    MODE(RW) OWNER(MVSCE02)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```

## Operator Commands

All commands are issued with the MVS MODIFY command:

```
F UFSD,command
```

### Status and Monitoring

| Command | Description |
|---------|-------------|
| `STATS` | Request counters, error count, POST bundles saved, free pool sizes, mounted filesystems with free block/inode counts |
| `SESSIONS` | List all active client sessions (userid, group, ASID, token, open FDs) |
| `HELP` | Print a summary of available commands |

### Mount Management

| Command | Description |
|---------|-------------|
| `MOUNT DSN=dsn,PATH=/path[,MODE=RW][,OWNER=user]` | Mount a dataset (DYNALLOC) |
| `MOUNT LIST` | List all currently mounted filesystems |
| `UNMOUNT PATH=/path` | Unmount the filesystem at the given path |

Dynamic mounts are not persisted — they are lost on UFSD restart. To make them permanent, add them to the Parmlib member.

### Diagnostics

| Command | Description |
|---------|-------------|
| `TRACE ON` | Enable per-request trace entries in the ring buffer |
| `TRACE OFF` | Disable tracing |
| `TRACE DUMP` | Dump the last 256 trace entries to the console |
| `REBUILD` | Force a full free-block and free-inode cache rebuild on all mounted RW disks |

### Maintenance

| Command | Description |
|---------|-------------|
| `SESSIONS PRUNE` | Release sessions for terminated address spaces (stale ASID detection via ASVT walk) |
| `SHUTDOWN` | Orderly shutdown (same as `/P UFSD`) |

### Examples

```
F UFSD,STATS
F UFSD,SESSIONS
F UFSD,MOUNT DSN=HTTPD.WEBROOT,PATH=/www,MODE=RW
F UFSD,MOUNT DSN=IBMUSER.UFSHOME,PATH=/u/ibmuser,MODE=RW,OWNER=IBMUSER
F UFSD,MOUNT LIST
F UFSD,UNMOUNT PATH=/www
F UFSD,TRACE ON
F UFSD,TRACE DUMP
F UFSD,REBUILD
F UFSD,SESSIONS PRUNE
```

## JCL Procedure

The STC procedure is provided in `samplib/ufsd`. Copy it to `SYS2.PROCLIB(UFSD)` and adjust the STEPLIB DSN:

```
//UFSD     PROC M=UFSDPRM0,
//            D='SYS2.PARMLIB'
//UFSD     EXEC PGM=UFSD,REGION=4M,TIME=1440
//STEPLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//SYSUDUMP DD  SYSOUT=*
//UFSDPRM  DD  DSN=&D(&M),DISP=SHR,FREE=CLOSE
```

### Symbolic Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `M` | `UFSDPRM0` | Parmlib member name |
| `D` | `SYS2.PARMLIB` | Parmlib dataset name |

Override at start time:

```
S UFSD                              default config
S UFSD,M=UFSDPRM1                   alternate config member
S UFSD,D='SYS1.PARMLIB'             alternate parmlib dataset
S UFSD,M=UFSDPRM1,D='MY.PARMLIB'   both overridden
```

## APF Authorization

UFSD allocates CSA storage (subpool 241, key 0) and operates in supervisor state. The STC authorizes itself at startup via `clib_apf_setup()`, so explicit APF authorization of the load library is typically not required on TK4- or TK5.

On hardened systems (RAKF or RACF with strict APF checking), authorize the load library explicitly using your system's standard method.

## Started Task User

UFSD needs OPER authority to allocate CSA storage and register the subsystem. On TK4-/TK5, `IBMUSER` already has sufficient authority.

To let MVS start the STC without a password, add to `SYS1.PARMLIB(IKJTSO00)`:

```
PROC(UFSD)
```
