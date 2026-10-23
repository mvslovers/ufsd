# Removing UFSD

This is the supported way to take UFSD back off an MVS 3.8j system and free
its FMID for a re-install. The same procedure is chapter 7, "Removing UFSD",
of the _UFSD User's Guide_ (ML05-0001).

The main way lets SMP do the work: a dummy function, `TUFSDEL`, deletes every
release of UFSD, and the load modules leave both libraries with it. A way that
uses `UCLIN` alone stays as the [fallback](#the-fallback-uclin-alone); the
upgrade routes use it too.

> **If you are holding a 1.2.0 archive, its removal instructions are wrong.**
> Its `docs/installation.md` says to run `RESTORE` and then `REJECT`. Both are
> refused once the FMID has been accepted, which the install job does in the
> same run as the APPLY. Every archive from 1.2.1 on points here instead.

> **If you are upgrading rather than removing, you are in the wrong document.**
> A release deletes its predecessor as part of its own install, and the
> scratch step below would delete the libraries the new release installs into.
> See
> [Upgrading](https://github.com/mvslovers/ufsd/blob/main/docs/installation.md#10-upgrading-from-an-earlier-release).

## What this release put on the system

| | |
|---|---|
| FMID | `TUFS151` |
| Load modules | `UFSD`, `UFSDSSIR`, `UFSDCLNP`, `UFSFMT` |
| Target library | `UFSD.LINKLIB` |
| Distribution library | `UFSD.AUFSDLOD` |
| Sample library | `UFSD.SAMPLIB` |

The data set names carry no version (issue #71), so there is only one UFSD
installation on a system, whichever release put it there. Releases before 1.3.0
put the version into the names (`UFSD.V1R2M2.LINKLIB` and so on). If you find
data sets of this kind, add the qualifier to every name below, and the FMID is
the one of that release (`TUFS120` for 1.2.x).

The staging library `UFSD.UFSDLOAD` is not on the list, because the cleanup
step of the install job scratched it.

**Earlier releases left their FMIDs behind.** Each upgrade deletes the release
before it and leaves its id in the inventory as a mark, in both zones:

```
TUFS150   TYPE            = FUNCTION
          DELBY           = TUFS151
```

A system that went from 1.2.x through 1.3.0, 1.4.0 and 1.5.0 to 1.5.1 carries
one such mark for every step. Deleting the id of the current release and of its
predecessor clears only the newest of them. Both ways below therefore name
**every id UFSD has ever used** (`TUFS110`, `TUFS120`, `TUFS130`, `TUFS140`,
`TUFS150` and `TUFS151`), and you need not know which releases the system has
seen. The jobs are written for releases up to 1.5.1. If a later release adds an
id, it is added to every list here.

---

## 1. Stop the server

```
/P UFSD
```

If common storage is still held afterwards, run `/S UFSDCLNP` before you go on.
See chapter 6, "Recovery After a Failure", of the _UFSD User's Guide_.

## 2. Delete the release with the dummy function

SMP deletes a function when a later function names it in a `DELETE` operand,
and it removes the load modules of the deleted function from the libraries on
the way. The job below receives, applies and accepts such a function.
`TUFSDEL` has no elements. It deletes all UFSD ids up to 1.5.1 in one `DELETE`
statement.

**`TUFSDEL` is not a release FMID.** No release of UFSD ships it, the job
brings it in by itself, and step 3 frees it again in the same flow. Do not use
it for anything else, and do not keep it on the system.

Two things in the job matter:

- The `APPLY` and the `ACCEPT` step each carry a `HMASMP.LINKLIB` and a
  `HMASMP.AUFSDLOD` statement. SMP deletes the modules from the libraries that
  these DD names point to. Without `HMASMP.AUFSDLOD`, the `ACCEPT` ends with
  return code 12 and the message `HMA2833 ** AUFSDLOD DDCARD MISSING`. Without
  `HMASMP.LINKLIB`, the `APPLY` still ends with return code 04 and prints the
  same `HMA2240` lines, but it deletes from the library that the procedure names
  itself (`SYS1.LINKLIB` on the systems measured), and `UFSD.LINKLIB` keeps its
  members. The member list of the last step is the only proof. The statements
  come after the `EXEC` statement and before `SMPCNTL`, as in the install job.
- The later steps run only on `COND=(4,LT,...)`. The `RECEIVE` ends with return
  code 04, because a function without elements raises `HMA3971`. With the usual
  `COND=(0,NE,...)` the job would stop right after the receive.

Submit this job:

```
//UFSDDEL  JOB (SYS),'UFSD DELETE',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//RECV    EXEC SMPREC
//SMPPTFIN DD  *
++FUNCTION(TUFSDEL) .
++VER(Z038) DELETE(TUFS110,TUFS120,TUFS130,TUFS140,TUFS150,TUFS151) .
/*
//SMPCNTL  DD  *
 RESETRC .
 RECEIVE SELECT(TUFSDEL) .
/*
//APPLY   EXEC SMPAPP,COND=(4,LT,RECV.HMASMP)
//HMASMP.LINKLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//HMASMP.AUFSDLOD DD  DISP=SHR,DSN=UFSD.AUFSDLOD
//SMPCNTL  DD  *
 APPLY S(TUFSDEL) DIS(WRITE) .
/*
//ACCEPT  EXEC SMPAPP,COND=(4,LT,APPLY.HMASMP)
//HMASMP.LINKLIB  DD  DISP=SHR,DSN=UFSD.LINKLIB
//HMASMP.AUFSDLOD DD  DISP=SHR,DSN=UFSD.AUFSDLOD
//SMPCNTL  DD  *
 ACCEPT S(TUFSDEL) DIS(WRITE) .
/*
//MEMBERS EXEC PGM=IKJEFT01
//SYSTSPRT DD  SYSOUT=*
//SYSTSIN  DD  *
 LISTDS 'UFSD.LINKLIB' MEMBERS
 LISTDS 'UFSD.AUFSDLOD' MEMBERS
/*
//
```

Read the three SMP steps one by one. The receive ends with return code 04:

```
 RECEIVE SELECT(TUFSDEL) .
----------++FUNCTION(TUFSDEL) .
----------++VER(Z038) DELETE(TUFS110,TUFS120,TUFS130,TUFS140,TUFS150,TUFS151) .
HMA3971    SYSMOD TUFSDEL HAS NO ELEMENTS
HMA3930    SYSMOD TUFSDEL SUCCESSFULLY RECEIVED
HMA2050    RECEIVE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

The apply deletes the four load modules from the target library. It also ends
with return code 04, because SMP does not find `TUFS151` in the restore data set
(`HMA2461`):

```
 APPLY S(TUFSDEL) DIS(WRITE) .
HMA2240    SUCCESSFULLY DELETED LMOD UFSD ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSDCLNP ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSDSSIR ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSFMT ON LINKLIB LIBRARY
HMA2270    APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TUFSDEL
HMA2461    SYSMOD TUFS151 NOT FOUND ON SMPSCDS LIBRARY
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

The accept deletes the four copies from the distribution library and ends with
return code 00:

```
 ACCEPT S(TUFSDEL) DIS(WRITE) .
HMA2240    SUCCESSFULLY DELETED MODULE UFSD ON AUFSDLOD LIBRARY
HMA2240    SUCCESSFULLY DELETED MODULE UFSDCLNP ON AUFSDLOD LIBRARY
HMA2240    SUCCESSFULLY DELETED MODULE UFSDSSIR ON AUFSDLOD LIBRARY
HMA2240    SUCCESSFULLY DELETED MODULE UFSFMT ON AUFSDLOD LIBRARY
HMA2270    ACCEPT PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TUFSDEL
HMA2050    ACCEPT PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

**The result is in the last step, not in the return codes.** Both libraries
must list no member. Everything else is the proof that the job ran, not that the
work happened:

```
 LISTDS 'UFSD.LINKLIB' MEMBERS
UFSD.LINKLIB
--RECFM-LRECL-BLKSIZE-DSORG
  U     **    15040   PO
--VOLUMES--
  TSO001
--MEMBERS--
 LISTDS 'UFSD.AUFSDLOD' MEMBERS
UFSD.AUFSDLOD
--RECFM-LRECL-BLKSIZE-DSORG
  U     **    15040   PO
--VOLUMES--
  TSO001
--MEMBERS--
```

If a step ends higher than 04, the following `APPLY` or `ACCEPT` step does not
run; the last step does, and shows what is still there. Read the output of the
failed step; do not repeat the job before you know what it did.

**What has been measured.** The job was run on a test system with a fresh
installation of release 1.5.1 as the only SMP product with UFSD on it, and on
one that went from 1.4.0 through 1.5.0 to 1.5.1 by the upgrade of the
installation guide. A system with more than one product in the same zones, and
a dummy function with a `++MAC` element instead of none have not been
measured. Whatever the system, the `LIST` in step 4 is the proof.

## 3. Free the ids

The dummy function has not made the ids free. It has left `TUFSDEL` itself in
both zones, with the status `REC APP ACC`, and a mark `DELBY = TUFSDEL` on every
id it deleted. This job removes the seven entries from the CDS (what is applied)
and the ACDS (what is accepted). It changes the two SMP data bases and no
library:

```
//UFSDUCL  JOB (SYS),'UFSD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLCDS  EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TUFSDEL) .
  DEL SYSMOD(TUFS151) .
  DEL SYSMOD(TUFS150) .
  DEL SYSMOD(TUFS140) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//UCLACDS EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN ACDS .
  DEL SYSMOD(TUFSDEL) .
  DEL SYSMOD(TUFS151) .
  DEL SYSMOD(TUFS150) .
  DEL SYSMOD(TUFS140) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TUFSDEL) .
 LIST ACDS SYSMOD(TUFSDEL) .
 LIST CDS  SYSMOD(TUFS151) .
 LIST ACDS SYSMOD(TUFS151) .
 LIST CDS  SYSMOD(TUFS150) .
 LIST ACDS SYSMOD(TUFS150) .
 LIST CDS  SYSMOD(TUFS140) .
 LIST ACDS SYSMOD(TUFS140) .
 LIST CDS  SYSMOD(TUFS130) .
 LIST ACDS SYSMOD(TUFS130) .
 LIST CDS  SYSMOD(TUFS120) .
 LIST ACDS SYSMOD(TUFS120) .
 LIST CDS  SYSMOD(TUFS110) .
 LIST ACDS SYSMOD(TUFS110) .
/*
//
```

Every `DEL` finds its entry, because the dummy function left a mark for every id
it named, also for those that the system never had. Both `UCLIN` steps end with
return code 00, and each `DEL` reports `HMA2550 UPDATE COMPLETE`. This is the
output of step `UCLCDS`:

```
 UCLIN CDS .
  DEL SYSMOD(TUFSDEL) .
HMA2550    UPDATE COMPLETE FOR TUFSDEL
  DEL SYSMOD(TUFS151) .
HMA2550    UPDATE COMPLETE FOR TUFS151
  DEL SYSMOD(TUFS150) .
HMA2550    UPDATE COMPLETE FOR TUFS150
  DEL SYSMOD(TUFS140) .
HMA2550    UPDATE COMPLETE FOR TUFS140
  DEL SYSMOD(TUFS130) .
HMA2550    UPDATE COMPLETE FOR TUFS130
  DEL SYSMOD(TUFS120) .
HMA2550    UPDATE COMPLETE FOR TUFS120
  DEL SYSMOD(TUFS110) .
HMA2550    UPDATE COMPLETE FOR TUFS110
 ENDUCL .
HMA2050    UCLIN PROCESSING COMPLETED - HIGHEST RETURN CODE IS 00
```

Step `UCLACDS` gives the same. Any other message is not expected.

## 4. Read the LIST — this is the actual result

The last step of the job is the result. For every id, in both zones, it must
answer that the entry was not found. These are the first two of the fourteen
statements, as the step printed them:

```
 RESETRC .
 LIST CDS  SYSMOD(TUFSDEL) .
LIST     SELECT SUMMARY REPORT FOR SMPCDS
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE FOR PROCESSING
 TYPE        NAME
 SYSMOD      TUFSDEL
 LIST ACDS SYSMOD(TUFSDEL) .
LIST     SELECT SUMMARY REPORT FOR SMPACDS
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE FOR PROCESSING
 TYPE        NAME
 SYSMOD      TUFSDEL
```

The step must end with the highest return code 04. **Return code 04 together
with this answer means that the FMID is free**, and `TUFSDEL` with it. Both
zones count: an id gone from one and still in the other is not free.

**Read the stanzas, not the return code of the step.** A mark that is still
there answers with return code 00, so it does not raise the return code of the
step above the 04 that you expect. The job has fourteen `LIST` statements, and
all fourteen must show the id as not found. A stanza that names
`TYPE = FUNCTION` is an entry that is still there, whether it shows the status
`REC APP ACC` or only a `DELBY` of its successor. This is what the statements
`LIST CDS SYSMOD(TUFS151)` and `LIST CDS SYSMOD(TUFSDEL)` print after step 2,
before step 3 has run:

```
 LIST CDS  SYSMOD(TUFS151) .
SMPCDS   SYSMOD ENTRIES
  NAME
TUFS151   TYPE            = FUNCTION
          DELBY           = TUFSDEL

 LIST CDS  SYSMOD(TUFSDEL) .
SMPCDS   SYSMOD ENTRIES
  NAME
TUFSDEL   TYPE            = FUNCTION
          STATUS          = REC  APP  ACC
          FMID            = TUFSDEL
          DATE/TIME REC   = 26.296  12:24:23
                    APP   = 26.296  12:24:46
                    ACC   = 26.296  12:25:02
          DELETE VER(001) = TUFS110   TUFS120   TUFS130   TUFS140   TUFS150   TUFS151
```

## 5. Scratch the libraries

After step 2 the two libraries are empty, but they still exist. The allocation
job of the installation creates both with `DISP=(NEW,CATLG)`, so it would fail
against either of them. Delete them in a job of your own:

```
  DELETE UFSD.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE UFSD.AUFSDLOD NONVSAM SCRATCH PURGE
```

**Do this only when you remove UFSD for good.** After an upgrade, these are the
data sets that the next release installs into; leave them. `UFSD.SAMPLIB` may
stay. The install job scratches it itself the next time.

## The fallback: UCLIN alone

Use this way when the dummy function cannot be used, and in the upgrade routes
of the installation guide, where the libraries must stay. After step 1, submit
this job instead of steps 2 and 3. It changes the two SMP data bases and no
library:

```
//UFSDUCL  JOB (SYS),'UFSD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLCDS  EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TUFS151) MOD(UFSD) .
  DEL SYSMOD(TUFS151) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS151) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS151) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL LMOD(UFSD) .
  DEL LMOD(UFSDSSIR) .
  DEL LMOD(UFSDCLNP) .
  DEL LMOD(UFSFMT) .
  DEL SYSMOD(TUFS151) .
  DEL SYSMOD(TUFS150) .
  DEL SYSMOD(TUFS140) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//UCLACDS EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN ACDS .
  DEL SYSMOD(TUFS151) MOD(UFSD) .
  DEL SYSMOD(TUFS151) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS151) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS151) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL SYSMOD(TUFS151) .
  DEL SYSMOD(TUFS150) .
  DEL SYSMOD(TUFS140) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TUFS151) .
 LIST ACDS SYSMOD(TUFS151) .
 LIST CDS  SYSMOD(TUFS150) .
 LIST ACDS SYSMOD(TUFS150) .
 LIST CDS  SYSMOD(TUFS140) .
 LIST ACDS SYSMOD(TUFS140) .
 LIST CDS  SYSMOD(TUFS130) .
 LIST ACDS SYSMOD(TUFS130) .
 LIST CDS  SYSMOD(TUFS120) .
 LIST ACDS SYSMOD(TUFS120) .
 LIST CDS  SYSMOD(TUFS110) .
 LIST ACDS SYSMOD(TUFS110) .
/*
//
```

Every `DEL` that finds its entry reports `HMA2550 UPDATE COMPLETE`. An id that
was never in the zone is reported as `HMA2532` and `HMA2560`, and the step ends
with return code 08. That is expected for the ids your system never had; the
statements after it are still processed. It is no sign that a line did what you
wanted, and a step that ends with return code 08 can also hide a statement that
failed for another reason. Read every message, and take the `LIST` of step 4 as
the only proof. This is the output of step `UCLCDS` on a system that had
installed 1.5.1 new, so that `TUFS140` and the older ids were never in its zone:

```
 UCLIN CDS .
  DEL SYSMOD(TUFS151) MOD(UFSD) .
HMA2550    UPDATE COMPLETE FOR TUFS151
  DEL SYSMOD(TUFS151) MOD(UFSDSSIR) .
HMA2550    UPDATE COMPLETE FOR TUFS151
  DEL SYSMOD(TUFS151) MOD(UFSDCLNP) .
HMA2550    UPDATE COMPLETE FOR TUFS151
  DEL SYSMOD(TUFS151) MOD(UFSFMT) .
HMA2550    UPDATE COMPLETE FOR TUFS151
  DEL MOD(UFSD) .
HMA2550    UPDATE COMPLETE FOR UFSD
  DEL MOD(UFSDSSIR) .
HMA2550    UPDATE COMPLETE FOR UFSDSSIR
  DEL MOD(UFSDCLNP) .
HMA2550    UPDATE COMPLETE FOR UFSDCLNP
  DEL MOD(UFSFMT) .
HMA2550    UPDATE COMPLETE FOR UFSFMT
  DEL LMOD(UFSD) .
HMA2550    UPDATE COMPLETE FOR UFSD
  DEL LMOD(UFSDSSIR) .
HMA2550    UPDATE COMPLETE FOR UFSDSSIR
  DEL LMOD(UFSDCLNP) .
HMA2550    UPDATE COMPLETE FOR UFSDCLNP
  DEL LMOD(UFSFMT) .
HMA2550    UPDATE COMPLETE FOR UFSFMT
  DEL SYSMOD(TUFS151) .
HMA2550    UPDATE COMPLETE FOR TUFS151
  DEL SYSMOD(TUFS150) .
HMA2550    UPDATE COMPLETE FOR TUFS150
  DEL SYSMOD(TUFS140) .
HMA2532 ** SYSMOD ENTRY TUFS140 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
  DEL SYSMOD(TUFS130) .
HMA2532 ** SYSMOD ENTRY TUFS130 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
  DEL SYSMOD(TUFS120) .
HMA2532 ** SYSMOD ENTRY TUFS120 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
  DEL SYSMOD(TUFS110) .
HMA2532 ** SYSMOD ENTRY TUFS110 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
 ENDUCL .
HMA2050    UCLIN PROCESSING COMPLETED - HIGHEST RETURN CODE IS 08
HMA2050    HMASMP PROCESSING COMPLETED - HIGHEST RETURN CODE IS 08
```

Step `UCLACDS` gives the same, without the `LMOD` statements. Any other message
is not expected.

**The two zones have a step each for a reason.** After a step has ended with
return code 08, SMP does not execute the next `UCLIN` of the same step: it
answers `HMA2293 ** CONTROL STATEMENT IGNORED DUE TO PREVIOUS ERROR` and does
nothing. With both blocks in one step, the ids that the system never had would
leave the ACDS untouched.

Then read the `LIST` as in step 4, here twelve statements without `TUFSDEL`, and,
if you remove UFSD for good, scratch the libraries as in step 5. `UCLIN` changes
only the inventory: until you scratch them, the load modules are still in the
target library, and the copies that SMP accepted are in the distribution
library.

## What is not removed, because SMP never owned it

- The procedures and the member of the configuration that you copied into the
  procedure library and the parameter library of the system.
- The entry for `UFSD.LINKLIB` in the APF list, if you made one.
- **Your file systems.** The data sets that `ROOT` and `MOUNT` name in the
  member hold your files. None of the steps above touches them.

Those are yours to delete.

---

## Why `RESTORE` and `REJECT` do not work

Worth knowing, because the messages point away from the cause. The install job
accepts the FMID in the same run in which it applies it. That closes both of the
usual ways back:

```
HMA2452 ** SYSMOD TUFS151 SELECTED FOR RESTORE HAS BEEN ACCEPTED
HMA2462 ** SYSMOD TUFS151 NOT FOUND ON SMPPTS LIBRARY
```

`RESTORE` refuses a SYSMOD that has been accepted (`HMA2452`, return code 12).
`REJECT` then fails for a reason that looks unrelated: `HMA2462` says that the
SYSMOD is not on the `SMPPTS` data set, although the job received it from there.

The cause, on the system where this was measured, is the option `PURGE=YES` in
the system entry of the `SMPPTS`. With it, SMP deletes a SYSMOD from the PTS as
soon as it has accepted it. The entry shows with `LIST PTS .`:

```
SMPPTS  SYSTEM ENTRY

SYSTEM    OPTIONS         = PAGELEN=0061  PEMAX=9999  PURGE=YES  REJECT=YES
```

Two things have not been measured: a system with `PURGE=NO`, and a `REJECT`
after the `ACCEPT` there. No `UCLIN` statement was found that changes the system
entry, so this document does not change it. It removes the FMID in ways that do
not need the PTS.

## Why the install job accepts at all

The `ACCEPT` fills the distribution library, which is the base a later
`RESTORE` of a **PTF** returns to. Without it, a `RESTORE` would delete the
module rather than revert it, because there would be no previous level to go
back to. The price is what this document is about: once the FMID has been
accepted, `RESTORE` refuses it and `REJECT` fails (on the system where this was
measured). The dummy function `TUFSDEL` and the `UCLIN` of step 3, as described
above, are what take it out again.
