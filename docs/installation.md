# UFSD Installation Guide

This guide installs UFSD on an MVS 3.8j system (TK4-, TK5, MVS/CE, or a custom
Hercules build) from the release distribution archive.

The installation is managed by **SMP Release 4** — the SMP that ships with
MVS 3.8j, not SMP/E. That means the system keeps a record of what was
installed, and there is a supported way back out (see
[Removing UFSD](#10-removing-ufsd)).

## Getting help

**Report problems as a GitHub issue:**
<https://github.com/mvslovers/ufsd/issues>

That is the only place bug reports are tracked. Please include the two build
stamps UFSD writes at startup (`UFSD000I` and `UFSD005I`, see
[step 8](#8-start-and-verify)) — they identify the exact build — plus the
console messages and, for an install problem, the job output.

**Questions and general support** are on Discord:
<https://discord.gg/gaUAFKGCR>

---

Two placeholders are used throughout:

| | |
|---|---|
| `<version>` | the release, e.g. `1.2.0` — it appears in every shipped file name |
| `<vrm>` | the same release as MVS dataset qualifier, e.g. `V1R2M0` |

Both are already filled in inside the shipped jobs; you only need them to
recognise which file is which.

---

## 1. What is in the archive

| File | What it is |
|------|------------|
| `README.md` | this guide |
| `ufsd-<version>-load.xmit` | the four load modules, as a TSO RECEIVE-ready XMIT |
| `ufsd-<version>-samplib.xmit` | the sample library: procedures, config pattern, format job |
| `ufsd-<version>-alloc.jcl` | allocates the datasets SMP installs into — **run once** |
| `ufsd-<version>-inst.jcl` | receives everything and installs it — repeatable |

The load modules are `UFSD` (the server), `UFSDSSIR` (the SSI router),
`UFSDCLNP` (recovery utility) and `UFSFMT` (disk format utility).

The separate release asset `ufsd-<version>-lib.tar.gz` is **not** part of this
archive and is not needed to install UFSD — it is for building client programs
against `libufs`, see [Building clients](#11-building-clients).

Where everything ends up:

```
UFSD.<vrm>.LINKLIB     the load modules -- point STEPLIB here
UFSD.<vrm>.SAMPLIB     the patterns you copy from in step 6
UFSD.<vrm>.AUFSDLOD    SMP's distribution library, the base a RESTORE returns to
```

---

## 2. Prerequisites

- **MVS 3.8j** on Hercules, up and IPLed, with SMP 4 usable (the `SMPREC` and
  `SMPAPP` procedures and the `SYS1.SMP*` datasets — present on TK4-/TK5 and
  MVS/CE as shipped).
- A userid authorised to submit jobs, to update a PROCLIB in the started-task
  concatenation, and to update a PARMLIB.
- A way to upload a **binary** file to the host — see step 3.
- Roughly 15 cylinders of DASD for the product datasets.

### Authorisation

UFSD enters supervisor state and works in key-0 CSA, so it has to run
authorised. It obtains that itself at startup (`clib_apf_setup()`), which is
why it runs on a stock TK4-/TK5 with no APF entry — but that path goes through
**SVC 244, which comes from RAKF**. On a system without RAKF it is not
available and the start ends with:

```
UFSD091E APF SETUP FAILED RC=n (STEPLIB NOT APF AUTHORIZED?)
```

The clean alternative is to add `UFSD.<vrm>.LINKLIB` to the APF list in
`SYS1.PARMLIB(IEAAPF00)`. Note that on MVS 3.8j the APF list is only read at
IPL, and that the library name carries the version — so this is one IPL per
release, not one IPL ever.

If neither applies to your system, resolve it before step 5: an install that
completes cleanly will still not start.

---

## 3. Upload the two XMIT files

Both `.xmit` files are EBCDIC NETDATA streams. Upload them **in binary** — no
ASCII/EBCDIC translation, no CRLF conversion — into sequential datasets with
`RECFM=FB LRECL=80 BLKSIZE=3120`.

**The dataset names are yours to choose.** The install job names them on its
own `RECEIVE` commands, so nothing depends on what you call them; you will
enter them once in step 5. This guide uses `IBMUSER.UFSD.LOAD.XMIT` and
`IBMUSER.UFSD.SAMP.XMIT`.

Whether you have to allocate them first depends on the upload path: the
mvslovers **ftpd** and **IND$FILE** create the dataset from the attributes you
supply, mvsMF and most other FTP servers need it to exist. Where a method says
*pre-allocate*, allocate both as `DSORG=PS, RECFM=FB, LRECL=80, BLKSIZE=3120`,
primary ~50 tracks, secondary 20 (TSO or ISPF 3.2).

Pick **one** method:

### a) FTP — mvslovers/ftpd (no pre-allocation)

If the system runs the mvslovers **ftpd** (default port 2121), log in, switch
to binary, and `put` — it creates the datasets:

```
ftp -P 2121 your-mvs-host
> binary
> put ufsd-<version>-load.xmit    'IBMUSER.UFSD.LOAD.XMIT'
> put ufsd-<version>-samplib.xmit 'IBMUSER.UFSD.SAMP.XMIT'
> quit
```

(A `quote site …` is accepted but its parameters are ignored — you do not need
it.)

### b) mvsMF (z/OSMF-compatible REST API), via zowe

If mvsMF is installed (e.g. you also run HTTPD/mvsMF). It requires the datasets
to **exist first**, so *pre-allocate*, then:

```
zowe zos-files upload file-to-data-set ufsd-<version>-load.xmit \
     "IBMUSER.UFSD.LOAD.XMIT" --binary
zowe zos-files upload file-to-data-set ufsd-<version>-samplib.xmit \
     "IBMUSER.UFSD.SAMP.XMIT" --binary
```

### c) Another FTP server (pre-allocate)

*Pre-allocate* both datasets, then transfer **binary** into them — a plain
`binary` + `put`. Any `SITE` keywords for dataset attributes vary between
MVS 3.8j TCP/IP stacks (they are **not** the z/OS syntax); pre-allocating makes
them unnecessary.

### d) IND$FILE (3270 emulator)

No pre-allocation needed. Use your emulator's file transfer in **binary** mode
(no ASCII/CRLF translation) with `RECFM=FB LRECL=80 BLKSIZE=3120`. The exact
option syntax is client-dependent — consult its file-transfer documentation.

---

## 4. Allocate the product datasets

Submit `ufsd-<version>-alloc.jcl` unchanged, unless you want a specific unit or
volume — the `UNIT=SYSDA` and the space on each DD are the only things worth
editing.

It creates `UFSD.<vrm>.LINKLIB` and `UFSD.<vrm>.AUFSDLOD` and nothing else. The
libraries the next step receives into are deliberately **not** allocated here:
TSO RECEIVE creates its own target and refuses to merge into an existing
dataset.

Expect `COND CODE 0000`.

> **Run this once.** There is no DELETE step in it, on purpose. After the
> install, `UFSD.<vrm>.AUFSDLOD` holds SMP's accepted copy of every module; a
> re-run that scratched it would leave the SMP inventory reporting an install
> that is no longer on the system, and nothing would say so. To start over,
> reject the SYSMOD first — see [Removing UFSD](#10-removing-ufsd).

---

## 5. Install

Open `ufsd-<version>-inst.jcl` and replace the two placeholder dataset names
with what you uploaded in step 3:

```
  RECEIVE INDSN('CHANGE.ME.UFSDLOAD') -      <- IBMUSER.UFSD.LOAD.XMIT
  RECEIVE INDSN('CHANGE.ME.SAMPLIB') -       <- IBMUSER.UFSD.SAMP.XMIT
```

Those are the only lines you have to change. Submit it.

The job runs eight steps, each conditional on the one before, so it stops at
the first failure rather than building on it:

| Step | What it does |
|------|--------------|
| `DELOLD` | scratches the RECEIVE targets, so the job can be re-run |
| `RECV1` | load XMIT → `UFSD.<vrm>.UFSDLOAD` (a staging library) |
| `RECV2` | samplib XMIT → `UFSD.<vrm>.SAMPLIB` |
| `RECV` | receives the SYSMOD into the SMP inventory |
| `APPLYCHK` | dry run — `APPLY` only proceeds if this ends RC 0 |
| `APPLY` | copies the load modules into `UFSD.<vrm>.LINKLIB` |
| `ACCEPT` | makes this level the base a later `RESTORE` returns to |
| `CLEANUP` | scratches the staging library, which is now spent |

The SYSMOD travels inline in the job — there is no third file to upload.

**What a good run looks like.** Every step `COND CODE 0000`, and in the SMP
output:

```
HMA3930    SYSMOD <fmid> SUCCESSFULLY RECEIVED
HMA2380    COPY SUCCESSFUL - MOD=UFSD - LMOD=UFSD - LIBRARY=LINKLIB
           - RETURN CODE=00                          (and one per module)
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

Then check `UFSD.<vrm>.LINKLIB` really holds `UFSD`, `UFSDSSIR`, `UFSDCLNP`
and `UFSFMT` (ISPF 3.4). Do look: SMP reports the library by **ddname**, and a
ddname says nothing about which dataset was behind it.

SMP **copies** these modules rather than re-binding them, which is why the
authorisation code and the link attributes are exactly what the build produced.

---

## 6. Install the procedures and the configuration

SMP does not touch your PROCLIB or PARMLIB, and that is deliberate: **the
product owns the patterns, your system owns the copies.** If SMP owned the
running procedure, every change you made to it would be silently replaced by
the next update. So this step is yours, and it is the one place where you have
to read what you are copying.

Copy from `UFSD.<vrm>.SAMPLIB`:

| Member | Copy to | Adjust |
|--------|---------|--------|
| `UFSD` | a PROCLIB in the started-task concatenation | usually nothing — `STEPLIB` already names this release's LINKLIB |
| `UFSDCLNP` | the same PROCLIB | likewise |
| `UFSDPRM0` | a PARMLIB | **yes — see below** |
| `UFSFMT` | anywhere you keep JCL; it is a job, not a procedure | dataset name and size, step 7 |

`SYS2.PROCLIB` is the usual home for the two procedures.

### Which PARMLIB

The shipped procedure defaults to `D='SYS2.PARMLIB'`. **On TK5 that dataset
does not exist** — put the member in `SYS1.PARMLIB` and either edit `D=` in
your copy of the procedure, or override it when starting:

```
/S UFSD,D='SYS1.PARMLIB'
```

On MVS/CE, `SYS2.PARMLIB` exists and the default is fine. `M=` selects the
member (default `UFSDPRM0`), so a second configuration can live alongside the
first.

### Editing UFSDPRM0

The shipped member is a **sample**, not a working configuration — it names
datasets from the development system. Every `ROOT` and `MOUNT` dataset must
already exist and be UFS-formatted before UFSD starts; it never allocates or
formats one.

```
ROOT     DSN(UFSD.ROOT)

MOUNT    DSN(HTTPD.WEBROOT)      PATH(/www)          MODE(RW)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```

- `ROOT` is required and is mounted on `/`.
- `MOUNT` mode is `RO` or `RW`; `OWNER(userid)` restricts writes to one user.
- Delete or comment out every `MOUNT` you are not going to create. A `#` in the
  first non-blank column comments a line out — it has to be first, because a
  `#` anywhere else is data (dataset names may contain one).

The full keyword reference is in
[configuration.md](https://github.com/mvslovers/ufsd/blob/main/docs/configuration.md).

---

## 7. Create the UFS disks

Each mounted filesystem is one dataset holding a formatted UFS image. At
minimum you need the `ROOT` dataset before the first start.

Use `UFSD.<vrm>.SAMPLIB(UFSFMT)`. It is a complete job that allocates the
dataset and formats it in two steps, on MVS, with nothing to install and
nothing to upload. Edit the dataset name, the `SPACE` and the `OWNER`, then
submit it once per filesystem.

The member carries a space-planning table in its comments. At `BLKSIZE=4096`,
256 blocks is about 1 MB and holds 62 files; the inode count scales with the
block count, and below 512 blocks a two-block minimum for the inode list caps
any disk at 62 files no matter how much space is free. Subdirectories consume
an inode too.

Two constraints the job explains and it is worth repeating:

- **Allocate a primary extent only.** UFSFMT formats the primary extent and
  ignores secondary allocations, so blocks in a secondary would never be
  formatted and never be reachable.
- **The dataset must be `DISP=OLD`.** UFSFMT refuses a `DISP=SHR` allocation
  rather than wipe a dataset someone else may be reading, and it will not
  overwrite an existing UFS filesystem unless `FORCE` is given.

It reports what it actually did as `UFSFMT26I` — MVS rounds an allocation up to
a track boundary, so the disk is often slightly larger than you asked for.

The full reference, including the control statements and the report, is in
[disk-setup.md](https://github.com/mvslovers/ufsd/blob/main/docs/disk-setup.md).

---

## 8. Start and verify

```
/S UFSD                      default member (UFSDPRM0)
/S UFSD,M=UFSDPRM1           alternate member
/S UFSD,D='SYS1.PARMLIB'     alternate PARMLIB -- TK5, see step 6
```

A healthy start ends in `UFSD001I … READY`:

```
UFSD000I UFSD <version> (A3F2C91) STARTING
UFSD005I LIBC370 V1.0.2 (22B4870)
UFSD030I CSA ALLOCATED: ANCHOR=00BDxxxx
UFSD034I SSCT REGISTERED, SUBSYSTEM NAME=UFS1
UFSD035I SSI ROUTER LOADED AT 00A8xxxx
UFSD060I MOUNTED UFSD.ROOT ON / (ROOT)
UFSD040I 3 FILESYSTEM(S) MOUNTED
UFSD001I UFSD <version> READY -- 3 DISKS, 78K CSA, 64 SESSIONS, 256 FILES
```

The two build stamps identify exactly what is running: `UFSD000I` gives the
version and the commit it was built from, `UFSD005I` the libc370 it was linked
against — quote both in a bug report. A build made from a modified working tree
marks its hash `-DIRTY` and adds `UFSD006W`; a released build never does.

Root reports as `(ROOT)`, not `RW`/`RO`: it is mounted read-write only long
enough to create the mount-point directories, then switched to read-only before
the first client can reach it. `UFSD040I` is a count — use `/F UFSD,MOUNT LIST`
for the live per-filesystem detail.

Operator commands:

```
/F UFSD,STATS                status, mounts, request/error counters, IN FLIGHT
/F UFSD,SESSIONS             active client sessions
/F UFSD,MOUNT LIST           per-filesystem detail
/F UFSD,HELP                 every MODIFY command
/P UFSD                      orderly stop
```

---

## 9. Recovery

A clean `/P UFSD` frees all CSA. After a cancel (`/C UFSD`) or an abend the
recovery handler deliberately **retains** the CSA, so that a dump still has
something to describe.

You do not normally have to do anything about it: `/S UFSD` reclaims the
retained CSA and the stale SSCT before it registers. Just start it again.

`UFSDCLNP` is the fallback for when that does not work — it deregisters the
SSCT, unloads the SSI router and frees every CSA pool:

```
/S UFSDCLNP                  (or submit it as a batch job, PGM=UFSDCLNP)
/S UFSD
```

It is safe to run when UFSD is not registered; it reports that there is nothing
to do and ends RC 0. Full procedures and the complete WTO message reference are
in [recovery.md](https://github.com/mvslovers/ufsd/blob/main/docs/recovery.md).

---

## 10. Removing UFSD

Because the installation is SMP-managed, there is a defined way back. Stop UFSD
first (`/P UFSD`), then run, in this order:

```
RESTORE S(<fmid>) .          takes the load modules back out of the LINKLIB
REJECT  SELECT(<fmid>) .     removes the SYSMOD from the SMP inventory
```

`RESTORE` runs under `SMPAPP` and `REJECT` under `SMPREC`; the DD statements
are the same ones the install job uses for `APPLY` — copy that step and change
the `SMPCNTL` command. The `<fmid>` is named at the top of the install job.

What SMP does **not** remove, because it never owned them: the copies you made
in step 6, and your UFS disks. Those are yours to delete.

---

## 11. Building clients

To build a client (HTTPD, FTPD, or your own program) against UFSD, unpack the
separate `ufsd-<version>-lib.tar.gz` release asset — it provides `include/` and
`lib/libufs.a` for the cc370 host build. The `libufs` C API and the assembler
linkage convention are documented in
[development.md](https://github.com/mvslovers/ufsd/blob/main/docs/development.md).

---

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Allocation job fails, dataset already exists | It was already run. Do not force it — see the warning in step 4 |
| `RECV1`/`RECV2` fails, target exists | Something else allocated it. RECEIVE refuses to merge; scratch it and re-run |
| `APPLYCHK` ends non-zero, `APPLY` skipped | Read the SMP output — the check exists to stop before anything is written. A missing DD is the usual cause |
| SMP reports success, but the module is not where you expected | A ddname says nothing about the dataset behind it. Check the JCL, then look at the library itself |
| `S806` (module not found) at `/S UFSD` | `STEPLIB` in the procedure does not name the LINKLIB the APPLY wrote to |
| `/S UFSD` rejected — procedure not found | Procedure not copied into a PROCLIB in the started-task concatenation |
| `UFSD091E APF SETUP FAILED` | No RAKF (so no SVC 244) and no APF entry — see step 2 |
| `UFSD061E PARMLIB … NOT FOUND` | `D=`/`M=` wrong, or the member is in a PARMLIB the procedure does not name. On TK5 this is usually `SYS2.PARMLIB` not existing — step 6 |
| Mount fails / `UFSD124E` superblock validation | The `ROOT`/`MOUNT` dataset does not exist or is not UFS-formatted — step 7 |
| `S106` at start on a freshly installed library | The XMIT was uploaded in text mode. Re-upload in **binary** and re-run the install job |

For diagnostics and the message-ID reference, see
[recovery.md](https://github.com/mvslovers/ufsd/blob/main/docs/recovery.md).
