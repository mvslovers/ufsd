# UFSD Installation Guide

This guide installs **UFSD 1.0.0** on an MVS 3.8j system (TK4-, TK5, MVS/CE, or
a custom Hercules build) from the release distribution archive.

The archive contains:

| File | Purpose |
|------|---------|
| `ufsd-1.0.0-load.xmit` | The load library (UFSD, UFSDSSIR, UFSDCLNP) as a TSO RECEIVE-ready XMIT |
| `samplib/ufsd` | Started-task procedure for the UFSD server |
| `samplib/ufsdclnp` | Started-task procedure for the emergency cleanup utility |
| `samplib/ufsdprm0` | Sample PARMLIB configuration member |
| `docs/installation.md` | This guide |

The optional `ufsd-1.0.0-lib.tar.gz` (host `libufs.a` + headers) is only needed
to **build client programs** — see [Building clients](#building-clients).

---

## 1. Prerequisites

- **MVS 3.8j** on Hercules, up and IPLed.
- A TSO userid with authority to update a PROCLIB (e.g. `SYS2.PROCLIB`) and a
  PARMLIB (e.g. `SYS1.PARMLIB`), and to define a started task.
- A 3270/TSO session for `RECEIVE` and operator commands.
- A way to upload a **binary** file to the host (any one of: the mvslovers
  ftpd, another FTP server, the mvsMF REST API, or a 3270 emulator with
  IND$FILE — see step 2).

UFSD runs as an **authorized started task** (it enters supervisor state for its
CSA work). On the target MVS 3.8j systems (TK4-/TK5/MVS-CE) this is provided by
the normal started-task security setup — no manual APF step is required.

---

## 2. Upload and RECEIVE the load library

The `.xmit` is an EBCDIC NETDATA stream. Upload it **in binary** (no
ASCII↔EBCDIC translation) into a **sequential dataset** with
`RECFM=FB LRECL=80 BLKSIZE=3120`, then TSO `RECEIVE` it.

How the target dataset gets created depends on the upload path: the mvslovers
**ftpd** and **IND$FILE** create it for you (from the attributes you give);
mvsMF and most other FTP servers need it **pre-allocated** first. Where a method
says *pre-allocate*, allocate `IBMUSER.UFSD.XMIT` (TSO or ISPF 3.2) as
`DSORG=PS, RECFM=FB, LRECL=80, BLKSIZE=3120`, primary ~50 tracks (secondary 20).

Pick **one** upload method:

### a) FTP — mvslovers/ftpd (no pre-allocation)

If the system runs the mvslovers **ftpd** (default port 2121), just log in,
switch to **binary**, and `put` — it creates the dataset for you:

```
ftp -P 2121 your-mvs-host
> binary
> put ufsd-1.0.0-load.xmit 'IBMUSER.UFSD.XMIT'
> quit
```

(A `quote site …` is accepted but its parameters are ignored — you don't need
it.)

### b) mvsMF (z/OSMF-compatible REST API), via zowe

If mvsMF is installed (e.g. you also run HTTPD/mvsMF) — the same path
`make deploy` uses. mvsMF requires the dataset to **exist first**, so
*pre-allocate* it, then:

```
zowe zos-files upload file-to-data-set ufsd-1.0.0-load.xmit "IBMUSER.UFSD.XMIT" --binary
```

### c) Another FTP server (pre-allocate)

*Pre-allocate* the FB/80/3120 dataset, then transfer **binary** into it — a
plain `binary` + `put`. Any `SITE` keywords for dataset attributes vary between
MVS 3.8j TCP/IP stacks (they are **not** the z/OS syntax); pre-allocating makes
them unnecessary.

### d) IND$FILE (3270 emulator)

No pre-allocation needed — IND$FILE creates the dataset from the attributes you
give in the transfer. Use your emulator's file transfer in **binary** mode (no
ASCII/CRLF translation) with `RECFM=FB LRECL=80 BLKSIZE=3120`. The exact option
syntax is **client/emulator-dependent** — consult its file-transfer docs.

### RECEIVE into the load library

`RECEIVE` unloads the XMIT into the load library, allocating it (`RECFM=U`)
from the attributes saved in the XMIT and restoring all three members. The
reliable, unattended form is a batch step under IKJEFT01:

```
//RECV     EXEC PGM=IKJEFT01
//SYSTSPRT DD  SYSOUT=*
//SYSTSIN  DD  *
  RECEIVE INDSN('IBMUSER.UFSD.XMIT') DATASET('UFSD.V1R0M0.LINKLIB')
/*
```

`DATASET(...)` names the target explicitly. Omit it to accept the DSN saved in
the XMIT (`UFSD.V1R0M0.LINKLIB`).

Interactively from TSO READY you can instead enter `RECEIVE
INDSN('IBMUSER.UFSD.XMIT')` and, when it prompts for restore parameters, press
**Enter** to accept the default DSN or type `DATASET('your.chosen.linklib')`.

Verify the three members are present:

```
ISPF 3.4 → UFSD.V1R0M0.LINKLIB  →  UFSD, UFSDSSIR, UFSDCLNP
```

Throughout the rest of this guide, replace `UFSD.V1R0M0.LINKLIB` with the DSN
you actually restored to.

---

## 3. Install the started-task procedures

Copy both procedures from the archive `samplib/` into your PROCLIB and point
their `STEPLIB` at the load library from step 2.

`SYS2.PROCLIB(UFSD)` — from `samplib/ufsd`:

```
//UFSD     PROC M=UFSDPRM0,D=SYS2.PARMLIB
//UFSD     EXEC PGM=UFSD,REGION=4M,TIME=1440
//STEPLIB  DD  DISP=SHR,DSN=UFSD.V1R0M0.LINKLIB      <- your load library
//UFSDPRM  DD  DISP=SHR,DSN=&D(&M),FREE=CLOSE
```

`SYS2.PROCLIB(UFSDCLNP)` — from `samplib/ufsdclnp`:

```
//UFSDCLNP PROC
//CLEANUP  EXEC PGM=UFSDCLNP
//STEPLIB  DD  DISP=SHR,DSN=UFSD.V1R0M0.LINKLIB      <- your load library
//SYSPRINT DD  SYSOUT=*
```

`M=` selects the PARMLIB member (default `UFSDPRM0`); `D=` selects the PARMLIB
dataset. Install **both** procs — UFSDCLNP is the supported recovery path after
an abend (see [step 7](#7-recovery)).

---

## 4. Configure PARMLIB

Copy `samplib/ufsdprm0` to `SYS1.PARMLIB(UFSDPRM0)` (or the `D=` dataset you set
above) and edit it for your site. The member declares the **root** filesystem
and any additional **mounts**:

```
/* SYS1.PARMLIB(UFSDPRM0) */

ROOT     DSN(UFSD.ROOT)

MOUNT    DSN(HTTPD.WEBROOT)      PATH(/wwwroot)      MODE(RO)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```

- `ROOT` is required and is mounted on `/`.
- `MOUNT` mode is `RO` or `RW`; `OWNER(userid)` restricts writes to one user.
- Comments are `/* … */`, or a `#` in the first non-blank column of a line
  (handy for disabling a `MOUNT`). A `#` elsewhere on the line is data, not a
  comment — dataset names may contain one.

Every `ROOT`/`MOUNT` dataset must **already exist and be UFS-formatted** before
you start UFSD (it does **not** allocate or format disks). See the next step.
The full keyword reference is in [configuration.md](configuration.md).

---

## 5. Create the UFS disk datasets

Each mounted filesystem is a BDAM dataset holding a formatted UFS image. Create
and upload them with **`ufsd-utils`** (from the `ufsd-utils` companion repo):

```
ufsd-utils create root.img --size 10M
ufsd-utils upload root.img UFSD.ROOT
```

Repeat for each `MOUNT` dataset in your PARMLIB. The complete workflow
(allocation attributes `DSORG=PS RECFM=U BLKSIZE=4096`, uploading content,
geometry) is documented in [disk-setup.md](disk-setup.md).

At minimum you need the **ROOT** dataset before the first start.

---

## 6. Start and verify

```
/S UFSD                     Start with the default member (UFSDPRM0)
/S UFSD,M=UFSDPRM1           Start with an alternate member
```

A healthy start ends in `UFSD001I … READY` (messages are upper-case on the
console):

```
UFSD000I UFSD 1.0.0 (A3F2C91) STARTING
UFSD005I LIBC370 V1.0.2 (22B4870)
UFSD030I CSA ALLOCATED: ANCHOR=00BDxxxx
UFSD034I SSCT REGISTERED, SUBSYSTEM NAME=UFSD
UFSD035I SSI ROUTER LOADED AT 00A8xxxx
UFSD060I MOUNTED UFSD.ROOT ON / (ROOT)
UFSD040I 3 FILESYSTEM(S) MOUNTED
UFSD001I UFSD 1.0.0 READY -- 3 DISKS, 78K CSA, 64 SESSIONS, 256 FILES
```

The two build stamps identify exactly what is running: `UFSD000I` gives the
UFSD version and the commit it was built from, `UFSD005I` the libc370 it was
linked against — quote both in a bug report. A build made from a modified
working tree marks its hash `-DIRTY` and adds `UFSD006W BUILT FROM A MODIFIED
WORKING TREE`; a released build never does.

Root reports as `(ROOT)`, not `RW`/`RO`: it is mounted read-write only long
enough to create the mount-point directories, then switched to read-only
before the first client can reach it. `UFSD040I` is a count — use
`/F UFSD,MOUNT LIST` for the live per-filesystem detail.

Operator commands:

```
/F UFSD,STATS               Status, mounts, request/error counters, IN FLIGHT
/F UFSD,SESSIONS            List active client sessions
/F UFSD,HELP                List all MODIFY commands
/P UFSD                     Orderly stop
```

The full command reference is in [configuration.md](configuration.md).

---

## 7. Recovery

A clean `/P UFSD` frees all CSA. After a **cancel (`/C UFSD`) or an abend**, the
recovery handler deliberately retains the CSA; reclaim it before restarting by
running UFSDCLNP:

```
/S UFSDCLNP                 (or submit it as a batch job, PGM=UFSDCLNP)
/S UFSD
```

UFSDCLNP quiesces any in-flight clients, then frees the SSCT, the SSI router,
the CSA pools and the anchor. Full recovery procedures and the complete WTO
message reference are in [recovery.md](recovery.md).

---

## Building clients

To build a client (HTTPD, FTPD, or your own program) against UFSD, unpack
`ufsd-1.0.0-lib.tar.gz` — it provides `include/` (the `libufs`/`ufsd` headers)
and `lib/libufs.a` for the cc370 host build. The `libufs` C API and the
assembler linkage convention are documented in
[development.md](development.md).

---

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| `S806` (module not found) at `/S UFSD` | `STEPLIB` DSN in the proc does not match the RECEIVE'd load library, or the members did not restore |
| `/S UFSD` rejected — procedure not found | Proc not copied into a PROCLIB in the started-task concatenation |
| `UFSD061E PARMLIB … NOT FOUND` | `UFSDPRM` DD / member missing, or `D=`/`M=` wrong |
| Mount fails / `UFSD124E` superblock validation | The `ROOT`/`MOUNT` dataset does not exist or is not UFS-formatted — create it first (step 5) |
| `S106` at start on a freshly restored library | The load modules were corrupted in transit — re-RECEIVE from a fresh **binary** upload |

For diagnostics and the message-ID reference, see [recovery.md](recovery.md).
