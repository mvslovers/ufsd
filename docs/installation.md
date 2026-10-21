# UFSD Installation Guide

This guide installs UFSD on an MVS 3.8j system (TK4-, TK5, MVS/CE, or a custom
Hercules build) from the release distribution archive.

The installation is managed by **SMP Release 4** — the SMP that ships with
MVS 3.8j, not SMP/E. That means the system keeps a record of what was
installed, and there is a supported way back out (see
[Removing UFSD](#11-removing-ufsd)).

## Getting help

**Report problems as a GitHub issue:**
<https://github.com/mvslovers/ufsd/issues>

That is the only place bug reports are tracked. Please include the two build
stamps UFSD writes at startup (`UFSD000I` and `UFSD005I`, see
[step 8](#8-start-and-verify)) — they identify the exact build — plus the
console messages and, for an install problem, the job output.

**Questions and general support** are on Discord:
<https://discord.gg/gaUAFKGCR>

## The manuals

This guide is complete for installing, upgrading and removing UFSD. For
everything else there are two manuals: the _UFSD User's Guide_ (ML05-0001) and
the _UFSD Reference_ (ML05-0002). They are attached to each release from 1.6.0
on as `ml05-0001-<edition>.pdf` and `ml05-0002-<edition>.pdf`, and their
sources are in `docs/books` in the repository. Where this guide sends you to a
chapter, it means one of these.

---

One placeholder is used throughout:

| | |
|---|---|
| `<version>` | the release, e.g. `1.5.0` — it appears in every shipped file name |

It is already filled in inside the shipped jobs; you only need it to recognise
which file is which.

The **dataset names carry no version**. `UFSD.LINKLIB` is `UFSD.LINKLIB` in
every release, so the `STEPLIB` in your procedure, any client JCL naming it,
and an APF entry for it are written once and never again (issue #71). The
trade is that there is no side-by-side install: an upgrade replaces what is
there. There is no way back to the previous release: the install job accepts
the new level, and SMP refuses a `RESTORE` of an accepted function (see
[section 11](#11-removing-ufsd)). To take UFSD out of the system, follow
section 11.

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
against `libufs`, see [Building clients](#12-building-clients).

Where everything ends up:

```
UFSD.LINKLIB     the load modules -- point STEPLIB here
UFSD.SAMPLIB     the patterns you copy from in step 6
UFSD.AUFSDLOD    SMP's distribution library (not a way back to the previous release)
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
**SVC 244, which comes from RAKF**. The start then shows
`UFSD007I AUTHORIZED BY SVC (MODULE KEY 8)`. On a system without RAKF the SVC
is not available and the start ends with:

```
UFSD091E APF SETUP FAILED RC=n (STEPLIB NOT APF AUTHORIZED?)
```

The clean alternative is to add `UFSD.LINKLIB` to the APF list in
`SYS1.PARMLIB(IEAAPF00)`. On MVS 3.8j the APF list is only read at IPL, so the
entry costs one IPL — once. The library name does not change between releases,
so no later upgrade asks for another one. The start then shows
`UFSD007I AUTHORIZED BY LIBRARY (MODULE KEY 0)`.

Release 1.5.0 was tested on the RAKF route. A start from an APF-authorized
library was not run for that release, and neither was changing the APF list on
MVS/CE.

The two routes are not quite the same underneath. UFSD is link-edited AC(1),
so from an APF-authorized library the job step is already authorized when
program fetch runs, and MVS obtains the job pack area in **key 0** — the
module's own storage is then read-only for the STC, which runs key 8. Via
SVC 244 the module is fetched key 8 and `JSCBAUTH` is set afterwards, which
does not relabel storage already allocated. UFSD therefore keeps no writable
data in module storage; releases up to 1.2.0 did, and abend S0C4 during
startup on the APF route (issue #64).

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

It creates `UFSD.LINKLIB` and `UFSD.AUFSDLOD` and nothing else. The libraries
the next step receives into are deliberately **not** allocated here: TSO
RECEIVE creates its own target and refuses to merge into an existing dataset.

Expect `COND CODE 0000`.

> **Run this once ever — not once per release.** Since the names carry no
> version, a later upgrade finds both datasets already there and must skip
> this step; running it again fails on `DISP=(NEW,CATLG)` and changes nothing.
>
> There is no DELETE step in it, on purpose. After the install,
> `UFSD.AUFSDLOD` holds SMP's accepted copy of every module; a re-run that
> scratched it would leave the SMP inventory reporting an install that is no
> longer on the system, and nothing would say so. To start over, remove the
> SYSMOD first — see [Removing UFSD](#11-removing-ufsd).

---

## 5. Install

Open `ufsd-<version>-inst.jcl` and replace the two placeholder dataset names
with what you uploaded in step 3:

```
  RECEIVE INDSN('CHANGE.ME.UFSDLOAD') -      <- IBMUSER.UFSD.LOAD.XMIT
  RECEIVE INDSN('CHANGE.ME.SAMPLIB') -       <- IBMUSER.UFSD.SAMP.XMIT
```

On TK5 those are the only lines you have to change. **On MVS/CE, TSO RECEIVE
also needs a volume to allocate on**: add a `VOLUME()` operand to both of these
`RECEIVE` commands (steps `RECV1` and `RECV2`) — `PUB000` on a stock MVS/CE,
or another volume of yours:

```
  RECEIVE INDSN('IBMUSER.UFSD.LOAD.XMIT') -
    DATASET('UFSD.UFSDLOAD') -
    VOLUME('PUB000')
```

Submit it.

The job runs eight steps, each conditional on the one before, so it stops at
the first failure rather than building on it:

| Step | What it does |
|------|--------------|
| `DELOLD` | scratches the RECEIVE targets, so the job can be re-run |
| `RECV1` | load XMIT → `UFSD.UFSDLOAD` (a staging library) |
| `RECV2` | samplib XMIT → `UFSD.SAMPLIB` |
| `RECV` | receives the SYSMOD into the SMP inventory |
| `APPLYCHK` | dry run — `APPLY` only proceeds if this ends RC 0 |
| `APPLY` | copies the load modules into `UFSD.LINKLIB` |
| `ACCEPT` | makes this level the base that the `RESTORE` of a later PTF returns to; closes `RESTORE` and `REJECT` for this release itself |
| `CLEANUP` | scratches the staging library, which is now spent |

The SYSMOD travels inline in the job — there is no third file to upload.

**What a good run looks like.** On a fresh install, every step `COND CODE 0000`,
and in the SMP output:

```
HMA3930    SYSMOD <fmid> SUCCESSFULLY RECEIVED
HMA2380    COPY SUCCESSFUL - MOD=UFSD - LMOD=UFSD - LIBRARY=LINKLIB
           - RETURN CODE=00                          (and one per module)
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

The status report that follows lists the predecessor as `DELETED` even on a
fresh install, because the SYSMOD names it in its `DELETE` operand:

```
SYSMOD   STATUS    TYPE      FMID     REQUISITE AND SUPEDBY SYSMODS
TUFS140  DELETED   FUNCTION
TUFS150  APPLIED   FUNCTION  TUFS150
```

An upgrade ends the `APPLY` with RC 04 (`HMA2461`) instead, and that is also
correct — see [section 10](#10-upgrading-from-an-earlier-release).

**Then list the members, and treat that as the result.** SMP reports the
library by **ddname**, and a ddname says nothing about which dataset was behind
it — and an install that owns none of its elements ends RC 00 from top to
bottom with an empty library. ISPF 3.4 does it, or submit this:

```
//UFSDLIST JOB (SYS),'UFSD MEMBERS',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1)
//LIST    EXEC PGM=IEHLIST
//SYSPRINT DD  SYSOUT=*
//DD1      DD  DISP=SHR,DSN=UFSD.LINKLIB
//SYSIN    DD  *
 LISTPDS DSNAME=UFSD.LINKLIB,VOL=SYSDA=<volser>
/*
//
```

`UFSD`, `UFSDSSIR`, `UFSDCLNP` and `UFSFMT` must all be there.

If the listing is empty while the job log said success, the `APPLY` printed
`NOT SEL` instead of `HMA2380`: some other SYSMOD owns those module names in
the SMP inventory. Each release deletes **only the one immediately before
it**, so the owner is cleared as part of the install only when you come from
that release. The usual cause is skipping a release: a system still on 1.3.0
(`TUFS130`) or 1.2.x (`TUFS120`) installing 1.5.0 -- see section 10, which
starts with the check that tells the cases apart. Otherwise the names were
claimed by something this release does not know about: a test install under a
throwaway id, or ufsd 1.1.x, which was never released but was applied on at
least one system under `TUFS110`. `LIST CDS MOD(UFSD) .` names the owner;
step 2 of
[uninstall.md](https://github.com/mvslovers/ufsd/blob/main/docs/uninstall.md)
removes it.

SMP **copies** these modules rather than re-binding them, which is why the
authorisation code and the link attributes are exactly what the build produced.

---

## 6. Install the procedures and the configuration

SMP does not touch your PROCLIB or PARMLIB, and that is deliberate: **the
product owns the patterns, your system owns the copies.** If SMP owned the
running procedure, every change you made to it would be silently replaced by
the next update. So this step is yours, and it is the one place where you have
to read what you are copying.

Copy from `UFSD.SAMPLIB`:

| Member | Copy to | Adjust |
|--------|---------|--------|
| `UFSD` | a PROCLIB in the started-task concatenation | usually nothing — `STEPLIB` already names `UFSD.LINKLIB` |
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

On MVS/CE `SYS2.PARMLIB` exists, so the default is expected to be right; that
was not checked on a running MVS/CE for this release. `M=` selects the member
(default `UFSDPRM0`), so a second configuration can live alongside the first.

### Editing UFSDPRM0

The shipped member is a **sample**, not a working configuration — it names
datasets from the development system. Every `ROOT` and `MOUNT` dataset must
already exist and be UFS-formatted before UFSD starts; it never allocates or
formats one.

```
ROOT     DSN(UFSD.ROOT)

MOUNT    DSN(HTTPD.WEBROOT.UFS)  PATH(/www)          MODE(RO)
MOUNT    DSN(IBMUSER.UFSHOME)    PATH(/u/ibmuser)    MODE(RW) OWNER(IBMUSER)
MOUNT    DSN(UFSD.SCRATCH)       PATH(/tmp)          MODE(RW)
```

This copy is abridged. The member in `UFSD.SAMPLIB` also has two more `MOUNT`
statements, for the home directories of `MVSCE01` and `MVSCE02`, a commented-out
`MOUNT` for `UFSD.ARCHIVE`, and comments.

`HTTPD.WEBROOT.UFS` is the web content disk that HTTPD's own installation guide
creates, mounted read-only on HTTPD's default `DOCROOT`, `/www`. If you do not
run HTTPD, delete that line.

- `ROOT` is required and is mounted on `/`.
- `MOUNT` mode is `RO` or `RW`; `OWNER(userid)` restricts writes to one user.
- Delete or comment out every `MOUNT` you are not going to create. A `#` in the
  first non-blank column comments a line out — it has to be first, because a
  `#` anywhere else is data (dataset names may contain one).

The statements are described in the _UFSD User's Guide_, chapter 4,
"Configuring UFSD", and in full in the _UFSD Reference_, chapter 2, "The
UFSDPRM Statements".

---

## 7. Create the UFS disks

Each mounted filesystem is one dataset holding a formatted UFS image. At
minimum you need the `ROOT` dataset before the first start.

Use `UFSD.SAMPLIB(UFSFMT)`. It is a complete job that allocates the
dataset and formats it in two steps, on MVS, with nothing to install and
nothing to upload. Edit the dataset name, the `SPACE` and the `OWNER`, then
submit it once per filesystem.

The member carries a space-planning table in its comments. At `BLKSIZE=4096`,
256 blocks is about 1 MB. The number of inodes is fixed when the disk is
formatted and follows the block count, but the inode list is never shorter than
two blocks: at the defaults a disk of up to 799 blocks (3.1 MB) holds at most
62 files, however much space is free. Subdirectories consume an inode too.

Two constraints the job explains and it is worth repeating:

- **Allocate a primary extent only.** UFSFMT formats the primary extent and
  ignores secondary allocations, so blocks in a secondary would never be
  formatted and never be reachable.
- **The dataset must be `DISP=OLD`.** UFSFMT refuses a `DISP=SHR` allocation
  rather than wipe a dataset someone else may be reading, and it will not
  overwrite an existing UFS filesystem unless `FORCE` is given. That check is
  skipped when the `BLKSIZE` of the dataset is outside 512 to 8192 (for
  instance 0 or 32760): then an existing filesystem is overwritten without
  `FORCE`. Give the same `BLKSIZE` in the allocation as in the format step.
  UFSD takes the block size from the data set label when it mounts, so a
  difference fails the mount with `UFSD062E` and `UFSD124E`.

It reports what it actually did as `UFSFMT26I` — MVS rounds an allocation up to
a track boundary, so the disk is often slightly larger than you asked for.

Planning, the control statements and the report are described in the _UFSD
User's Guide_, chapter 3, "Creating Disks", and in the _UFSD Reference_,
chapter 3, "The UFSFMT Utility".

---

## 8. Start and verify

```
/S UFSD                      default member (UFSDPRM0)
/S UFSD,M=UFSDPRM1           alternate member
/S UFSD,D='SYS1.PARMLIB'     alternate PARMLIB -- TK5, see step 6
```

A healthy start ends in `UFSD001I … READY`. The set of lines depends on your
configuration; this is the console of a 1.5.0 system with three filesystems.
The version, the build stamps and the addresses are those of that system:

```
UFSD000I UFSD 1.5.0 (1AA3EB6) STARTING
UFSD005I LIBC370 2.24.1 (EF09210)
UFSD007I AUTHORIZED BY SVC (MODULE KEY 8)
UFSD030I CSA ALLOCATED: ANCHOR=00BBE7F8
UFSD031I   REQUEST POOL: 32 BLOCKS, 9K
UFSD032I   BUFFER POOL:  16 BLOCKS, 64K
UFSD033I   TRACE BUFFER: 256 ENTRIES, 4K
UFSD034I SSCT REGISTERED, SUBSYSTEM NAME=UFS1
UFSD035I SSI ROUTER LOADED AT 00ACD4C8
UFSD045I SESSION TABLE: 64 SLOTS
UFSD047I GLOBAL FILE TABLE: 256 SLOTS
UFSD060I MOUNTED UFSD.ROOT ON / (ROOT)
UFSD060I MOUNTED UFSD.SCRATCH ON /tmp (RW)
UFSD060I MOUNTED HERC01.UFSHOME ON /u/herc01 (RW, OWNER=HERC01)
UFSD040I 3 FILESYSTEM(S) MOUNTED
UFSD001I UFSD 1.5.0 READY -- 3 DISKS, 78K CSA, 64 SESSIONS, 256 FILES
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

The commands are described in the _UFSD User's Guide_, chapter 5, "Running
UFSD", and in full in the _UFSD Reference_, chapter 1, "Operator Commands".

---

## 9. Recovery

A clean `/P UFSD` frees all CSA. After a cancel (`/C UFSD`), an abend, or a
stop that found clients still at work, the CSA is deliberately **retained**,
so that a dump still has something to describe. The console shows `UFSD098W`
for the stop with clients, and `UFSD098E` and then `UFSD097W` for a cancel or
an abend.

You do not normally have to do anything about it: `/S UFSD` reclaims the
retained CSA and the stale SSCT before it registers. Just start it again.

The start refuses to reclaim when the server it finds is still running
(`UFSD155W`, `UFSD002E`), and when it cannot check that (`UFSD156W`).
`UFSDCLNP` frees the same storage without starting a server. Use it when the
start refuses with `UFSD156W` and you know that no server runs:

```
/S UFSDCLNP                  (or submit it as a batch job, PGM=UFSDCLNP)
/S UFSD
```

It is safe to run when UFSD is not registered; it reports that there is nothing
to do and ends RC 0. When the server is running it refuses with `UFSD157W` and
RC 8.

Recovery is described in the _UFSD User's Guide_, chapter 6, "Recovery After a
Failure"; the messages are in the _UFSD Reference_, appendix A.

---

## 10. Upgrading from an earlier release

**First ask SMP which FMID owns the modules**, because each release deletes
only the one immediately before it:

```
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  MOD(UFSD) .
 LIST ACDS MOD(UFSD) .
/*
```

- **`TUFS140`:** an upgrade from 1.4.0. Run `/P UFSD`, the install job, then `/S UFSD`.
- **`TUFS130`:** 1.3.0 — this release skips 1.4.0 and so does not delete it.
  See [Coming from 1.3.0](#coming-from-130).
- **`TUFS120`:** 1.2.x. See [Coming from 1.2.x](#coming-from-12x).
- **not found:** a fresh install, or a system that never had UFSD under SMP —
  see [Coming from a non-SMP install](#coming-from-a-non-smp-install) for the
  latter. Run the allocation job, then the install job.

Each release has an FMID of its own and deletes only its predecessor's:
`TUFS140` is 1.4.0, and `TUFS150` (this release) deletes `TUFS140`. An older
owner of `MOD(UFSD)` is left in place, and then SMP installs nothing.

An upgrade from the immediately preceding release is **step 3 and step 5, and
nothing else**. The dataset names do
not change between releases, and the SYSMOD removes its predecessor on its own
— there is no inventory work for you to do first.

**Skip step 4.** `UFSD.LINKLIB` and `UFSD.AUFSDLOD` are already there. The
alloc job would fail on `DISP=(NEW,CATLG)` — harmlessly, but it has nothing to
do.

**There is no `UCLIN` step any more.** Each release carries its own FMID and
deletes the one before it — 1.5.0 is `TUFS150` and its `++VER` reads
`DELETE(TUFS140)`. SMP deletes the predecessor's load modules from the target
library and copies the new ones in, and the module entries in its inventory
become this release's. The `UCLIN` job in
[uninstall.md](https://github.com/mvslovers/ufsd/blob/main/docs/uninstall.md)
is for **removing** UFSD, not for replacing it — running it as part of an
upgrade would scratch the datasets you are installing into.

**Stop the server.** `/P UFSD` before the install job, `/S UFSD` after. A
running STC holds its own copy of the load module in storage and would go on
using it, so an upgrade that skips this looks like it did nothing.

**Expect RC 04 from the APPLY, and let it stand.** A deleted FMID has no
backup entry in `SYS1.SMPSCDS` and never will, so the step reports success and
*then* a 4:

```
HMA2270    APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TUFS150
HMA2461    SYSMOD TUFS140 NOT FOUND ON SMPSCDS LIBRARY
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

That is the expected result of a replacement, and the job's `ACCEPT` step is
conditioned to run through it.

**Check the members, not the condition codes.** The messages that prove the
upgrade happened are, per module:

```
HMA2240    SUCCESSFULLY DELETED LMOD UFSD ON LINKLIB LIBRARY
HMA2380    COPY SUCCESSFUL - MOD=UFSD - LMOD=UFSD - LIBRARY=LINKLIB
```

In an upgrade, the element reports that follow can show `NOT SEL` next to
`TUFS140 DELETED`. That is normal; what shows that the upgrade happened is the
pair `HMA2240` and `HMA2380` for each module. A `NOT SEL` *in place of*
`HMA2380` is the failure described next.

If SMP meets an element it does not own it prints `NOT SEL` instead, copies
nothing, and still ends RC 00 with `HMA2270 ... SUCCESSFULLY COMPLETED` — an
install that reports success and changes nothing. Read `UFSD000I` after the
restart as the final word: it names the version and the commit the running
module was built from. (`UFSD005I` reports the libc370 it was linked against,
not the UFSD build -- it will not tell you whether the upgrade took.)

### Coming from 1.2.x

1.2.0, 1.2.1 and 1.2.2 put their datasets in `UFSD.V1R2M0.*`, `UFSD.V1R2M1.*`
or `UFSD.V1R2M2.*`, and all three shipped under the same FMID `TUFS120`. Later
releases install into `UFSD.*` instead, so the first upgrade across the rename
is a **move**: the old datasets are not written to, and you scratch them
yourself once the new server is up.

**Only 1.3.0 deletes `TUFS120`.** Every release deletes the one immediately
before it and nothing older, so this release (`TUFS150`, `DELETE(TUFS140)`)
leaves `TUFS120` owning `MOD(UFSD)`. SMP then prints `NOT SEL` for every
module, copies nothing and ends `RC 00`: the trap described under section 5 of this guide.
Release the inventory by hand first, in step 2.

1. `/P UFSD`.
2. **Step 2 of [uninstall.md](https://github.com/mvslovers/ufsd/blob/main/docs/uninstall.md#2-cut-the-fmid-out-of-the-smp-inventory)
   only** -- the `UCLIN` job, which edits the CDS and the ACDS and touches no
   library. Read its `LIST` as step 3 there says. **Do not run its step 4**,
   which scratches libraries.
3. **Step 4 of this guide** — the alloc job creates `UFSD.LINKLIB` and
   `UFSD.AUFSDLOD` beside the old ones. This is the one upgrade that runs it.
4. **Steps 3 and 5**, unchanged. After step 2, this is a fresh install as far
   as SMP is concerned. **It does not touch the old datasets.** SMP records the
   *ddname* an element was installed through and never the dataset behind it,
   so it resolves `LINKLIB` in this job — where it names the new library — and
   copies the new modules there. `UFSD.V1R2Mx.LINKLIB` is never opened. This is
   SMP's behaviour, not UFSD's.
5. Re-point what names the library: `STEPLIB` in your `UFSD` and `UFSDCLNP`
   procedures, any client JCL, and — if you took the APF route — the entry in
   `SYS1.PARMLIB(IEAAPF00)`. **That is one more IPL, and the last one**: the
   name does not change again.
6. `/S UFSD`, then read `UFSD000I` (step 8). **It must name the version and
   commit you just installed.** If it still names the old build, step 5 did not
   take -- the STC is loading out of a library you did not re-point -- and
   nothing below this line is safe to do yet.
7. Only once `UFSD000I` names the new build, scratch the release you came
   from:
   `DELETE UFSD.V1R2Mx.LINKLIB / .AUFSDLOD / .SAMPLIB NONVSAM SCRATCH PURGE`.
   Copy anything you still want out of the old SAMPLIB first.

> **Steps 5 and 7 are the upgrade, not housekeeping.** The old library survives
> the install untouched — still populated, still a working load module, and
> still APF-authorised if it was before. Nothing in the job log mentions it. A
> `STEPLIB` that was not re-pointed goes on loading the **old** server, an
> install that reported success from top to bottom will look like it did
> nothing, and `UFSD000I` will report the previous build. If you stop after
> step 4 you have not upgraded.

The gate in step 6 is the point of the sequence: **an upgrade is proven by a
positive statement, never by the absence of a failure.** Nothing above it
reports the old server still running, so there is no error to wait for --
`UFSD000I` naming the new commit is the only evidence that exists.

Steps 5 to 7 are therefore a **deliberate rollback window, then a scratch**:
both installations sit on the disk side by side until you end it, so a bad
start at step 6 is one procedure edit away from being undone. Treat it as a
window you chose and then close — not as a state you can leave the system in.

Each step was measured on its own (the `UCLIN`, a fresh install, and 1.3.0's
move across the rename). The sequence as a whole has not been run from 1.2.x to
this release. The alternative is to walk the releases in order — 1.3.0, then
1.4.0, then this one — each an ordinary upgrade after the first.

### Coming from 1.3.0

1.3.0 (`TUFS130`) already installs into `UFSD.*`, so there is no rename to
cross: nothing to allocate and no `STEPLIB` to re-point. What it shares with
1.2.x is the inventory wall — this release deletes `TUFS140` only, so
`TUFS130` still owns `MOD(UFSD)` and a plain install prints `NOT SEL` and ends
`RC 00` with nothing copied.

Either install 1.4.0 first and then this release (two ordinary upgrades), or
release the inventory by hand:

1. `/P UFSD`.
2. **Step 2 of [uninstall.md](https://github.com/mvslovers/ufsd/blob/main/docs/uninstall.md#2-cut-the-fmid-out-of-the-smp-inventory)
   only**, as in the 1.2.x route above; do not run its step 4.
3. **Steps 3 and 5**, then `/S UFSD` and read `UFSD000I` (step 8): it must name
   the version and commit you just installed.

The hand route has not been run from 1.3.0 to this release; the `UCLIN` it
relies on has been measured on its own.

### Coming from a non-SMP install

Before 1.2.0, UFSD was installed with a plain TSO `RECEIVE` into a load
library of the site's choosing (`UFSD.V1R0M0.LINKLIB` in the 1.1.0 guide), with
no SMP involved. SMP has no record of such an install, so the check at the top of
this section finds nothing, and there is nothing to release. The route that
worked is a fresh SMP install beside the old one, then a switch-over:

1. Do a fresh install: **steps 3 to 5** of this guide, the allocation job
   included. The old load library stays as it is.
2. Re-point what names the library: `STEPLIB` in your `UFSD` and `UFSDCLNP`
   procedures, any client JCL, and the APF entry if you took the APF route
   (one IPL, as in step 5 of [Coming from 1.2.x](#coming-from-12x)).
3. `/P UFSD`, then `/S UFSD`, and read `UFSD000I` (step 8): it must name the
   version and commit you just installed. If it still names the old build, the
   `STEPLIB` was not re-pointed.
4. Only then scratch the old load library.

Your `ROOT` and `MOUNT` datasets and your PARMLIB member are not touched by any
of this.

The install job writes `UFSD.LINKLIB`, `UFSD.AUFSDLOD`, `UFSD.UFSDLOAD` and
`UFSD.SAMPLIB`. If your old install already uses one of those names, stop
first: the allocation job fails on an existing `UFSD.LINKLIB` or
`UFSD.AUFSDLOD`, and the install job's `DELOLD` step **scratches**
`UFSD.UFSDLOAD` and `UFSD.SAMPLIB`. Copy what you want out of them, or move the
old library to another name. That case has not been run.

---

## 11. Removing UFSD

Because the installation is SMP-managed, there is a defined way back — but it
is **not** the `RESTORE` followed by `REJECT` that SMP documentation leads you
to expect. The install job accepts the FMID in the same run as the APPLY, and
an accepted function SYSMOD refuses both: `RESTORE` because it was accepted,
`REJECT` because accepting removed the control statements it works from. The
route that does work is a `UCLIN` job.

The full procedure, with the job to submit and the messages to check, is in
[uninstall.md](https://github.com/mvslovers/ufsd/blob/main/docs/uninstall.md).

What SMP does **not** remove, because it never owned them: the copies you made
in step 6, and your UFS disks. Those are yours to delete.

---

## 12. Building clients

To build a client (HTTPD, FTPD, or your own program) against UFSD, unpack the
separate `ufsd-<version>-lib.tar.gz` release asset — it provides `include/` and
`lib/libufs.a` for the cc370 host build. The `libufs` C API and the assembler
linkage convention are documented in
[development.md](https://github.com/mvslovers/ufsd/blob/main/internals/development.md).

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
| `S0C4` right after `UFSD047I`, only with an APF-authorized LINKLIB | Release 1.2.0 or earlier. Upgrade, or drop the APF entry and let SVC 244 do it |
| `UFSD100W`, then `UFSD061E PARMLIB (DD:UFSDPRM) UNUSABLE -- SHUTTING DOWN` | The member cannot be opened: `D=` or `M=` is wrong, or the member is in a PARMLIB the procedure does not name. On TK5 this is usually `SYS2.PARMLIB` not existing — step 6. The text of `UFSD100W` says `USING DEFAULTS`, but there are none: the start ends with RC 8 |
| `UFSD106E` instead of `UFSD100W` | The member was found but could not be read to the end — a medium or DCB problem, not a configuration one |
| `UFSD105W PARMLIB: ROOT STATEMENT MISSING` and `UFSD061E` | The member has no `ROOT` statement |
| `UFSD120E` for the root or a mount | The data set cannot be allocated: it does not exist, or it is in use. Create it first — step 7 |
| `UFSD044E ddname: NOT A VALID UFS DISK (TYPE=xxxx)` | The data set exists, but its first block is not that of a UFS file system. It was not formatted with UFSFMT — step 7 |
| `UFSD062E` or `UFSD124E` | A check of the superblock failed. The data set was not formatted with UFSFMT, or it is damaged |
| `S106` at start on a freshly installed library | The XMIT was uploaded in text mode. Re-upload in **binary** and re-run the install job |

The meaning of every message, with the action to take, is in the _UFSD
Reference_, appendix A, "Messages". For what to do after a failure, see
step 9 and the _UFSD User's Guide_, chapter 6, "Recovery After a Failure".
