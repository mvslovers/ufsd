# Removing UFSD

This is the supported way to take UFSD back off an MVS 3.8j system and free
its FMID for a re-install. The same procedure is chapter 8, "Removing UFSD",
of the _UFSD User's Guide_ (ML05-0001).

The main way lets SMP do the work: a dummy function, `TUFSDEL`, deletes every
release of UFSD, and the load modules leave both libraries with it. A way that
uses `UCLIN` alone stays as the [fallback](#the-fallback-uclin-alone); the
upgrade routes use it too.

> **If you are holding a 1.2.0 archive, its removal instructions are wrong.**
> Its `docs/installation.md` says to run `RESTORE` and then `REJECT`. Both are
> refused once the FMID has been accepted, which the install job does in the
> same run as the APPLY. Every archive from 1.2.1 on points here instead.

> **Did you install a development build of `main`?** Its FMID is `TUFS160`, which
> none of the jobs below names. Go to
> [the section on development builds](#you-installed-a-development-build-with-a-later-fmid)
> first.

> **If you are upgrading rather than removing, you are in the wrong document.**
> A release deletes its predecessor as part of its own install, and the
> scratch step below would delete the libraries the new release installs into.
> See
> [Upgrading](https://github.com/mvslovers/ufsd/blob/main/docs/installation.md#10-upgrading-from-an-earlier-release).

## What this release put on the system

| | |
|---|---|
| FMID | `TUFS152` |
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
TUFS151   TYPE            = FUNCTION
          DELBY           = TUFS152
```

A system that went from 1.2.x through 1.3.0, 1.4.0, 1.5.0 and 1.5.1 to 1.5.2
carries one such mark for every step. Deleting the id of the current release
and of its predecessor clears only the newest of them. Both ways below
therefore name **every id UFSD has ever used** (`TUFS110`, `TUFS120`,
`TUFS130`, `TUFS140`, `TUFS150`, `TUFS151` and `TUFS152`), and you need not
know which releases the system has seen. The jobs are written for releases up
to 1.5.2. If a later release adds an id, it is added to every list here.

---

## 1. Stop the server

```
/P UFSD
```

If common storage is still held afterwards, run `/S UFSDCLNP` before you go on.
See chapter 7, "Recovery After a Failure", of the _UFSD User's Guide_.

## 2. Delete the release with the dummy function

SMP deletes a function when a later function names it in a `DELETE` operand,
and it removes the load modules of the deleted function from the libraries on
the way. The job below receives, applies and accepts such a function.
`TUFSDEL` has no elements. It deletes all UFSD ids up to 1.5.2 in one `DELETE`
statement.

**`TUFSDEL` is not a release FMID.** No release of UFSD ships it, the job
brings it in by itself, and step 3 frees it again in the same flow. Do not use
it for anything else, and do not keep it on the system.

Three things in the job matter:

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
- The `++VER` statement is on two lines, and it has to stay so. SMP reads
  columns 1 to 72 of a card and nothing after them. With all seven ids on one
  line, the last id crosses column 72, and the `RECEIVE` ends with return code
  08 and the messages `HMA2032 ** SYNTAX ERROR IN CONTROL STATEMENT AT COL 68`
  and `HMA3962 ** SYSMOD TUFSDEL HAS NO ++VER MODIFICATION CONTROL STATEMENT`.
  The `APPLY` step is then flushed, and the `ACCEPT` step runs all the same and
  ends with return code 12 (`HMA4012 ** SYSMOD TUFSDEL SELECTED FOR ACCEPT NOT
  FOUND ON SMPPTS LIBRARY`). Nothing is removed. A statement with six ids, as
  in the earlier releases of this document, fitted on the line.

Submit this job:

```
//UFSDDEL  JOB (SYS),'UFSD DELETE',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//RECV    EXEC SMPREC
//SMPPTFIN DD  *
++FUNCTION(TUFSDEL) .
++VER(Z038) DELETE(TUFS110,TUFS120,TUFS130,TUFS140,TUFS150,
   TUFS151,TUFS152) .
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
----------++VER(Z038) DELETE(TUFS110,TUFS120,TUFS130,TUFS140,TUFS150,
----------   TUFS151,TUFS152) .
HMA3971    SYSMOD TUFSDEL HAS NO ELEMENTS
HMA3930    SYSMOD TUFSDEL SUCCESSFULLY RECEIVED
HMA2050    RECEIVE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

The apply deletes the four load modules from the target library. It also ends
with return code 04, because SMP does not find `TUFS152` in the restore data set
(`HMA2461`):

```
 APPLY S(TUFSDEL) DIS(WRITE) .
HMA2240    SUCCESSFULLY DELETED LMOD UFSD ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSDCLNP ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSDSSIR ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSFMT ON LINKLIB LIBRARY
HMA2270    APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TUFSDEL
HMA2461    SYSMOD TUFS152 NOT FOUND ON SMPSCDS LIBRARY
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
installation of release 1.5.2 as the only SMP product with UFSD on it, and on
one that went from a fresh 1.5.1 to 1.5.2 by the upgrade of the installation
guide. A system with more than one product in the same zones, and a dummy
function with a `++MAC` element instead of none have not been measured.
Whatever the system, the `LIST` in step 4 is the proof.

## 3. Free the ids

The dummy function has not made the ids free. It has left `TUFSDEL` itself in
both zones, with the status `REC APP ACC`, and a mark `DELBY = TUFSDEL` on every
id it deleted. This job removes the eight entries from the CDS (what is applied)
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
  DEL SYSMOD(TUFS152) .
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
  DEL SYSMOD(TUFS152) .
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
 LIST CDS  SYSMOD(TUFS152) .
 LIST ACDS SYSMOD(TUFS152) .
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
  DEL SYSMOD(TUFS152) .
HMA2550    UPDATE COMPLETE FOR TUFS152
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
answer that the entry was not found. These are the first two of the sixteen
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
step above the 04 that you expect. The job has sixteen `LIST` statements, and
all sixteen must show the id as not found. A stanza that names
`TYPE = FUNCTION` is an entry that is still there, whether it shows the status
`REC APP ACC` or only a `DELBY` of its successor. This is what the statements
`LIST CDS SYSMOD(TUFS152)` and `LIST CDS SYSMOD(TUFSDEL)` print after step 2,
before step 3 has run:

```
 LIST CDS  SYSMOD(TUFS152) .
SMPCDS   SYSMOD ENTRIES
  NAME
TUFS152   TYPE            = FUNCTION
          DELBY           = TUFSDEL

 LIST CDS  SYSMOD(TUFSDEL) .
SMPCDS   SYSMOD ENTRIES
  NAME
TUFSDEL   TYPE            = FUNCTION
          STATUS          = REC  APP  ACC
          FMID            = TUFSDEL
          DATE/TIME REC   = 26.299  22:30:05
                    APP   = 26.299  22:30:23
                    ACC   = 26.299  22:30:37
          DELETE VER(001) = TUFS110   TUFS120   TUFS130   TUFS140   TUFS150   TUFS151   TUFS152
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
  DEL SYSMOD(TUFS152) MOD(UFSD) .
  DEL SYSMOD(TUFS152) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS152) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS152) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL LMOD(UFSD) .
  DEL LMOD(UFSDSSIR) .
  DEL LMOD(UFSDCLNP) .
  DEL LMOD(UFSFMT) .
  DEL SYSMOD(TUFS152) .
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
  DEL SYSMOD(TUFS152) MOD(UFSD) .
  DEL SYSMOD(TUFS152) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS152) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS152) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL SYSMOD(TUFS152) .
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
 LIST CDS  SYSMOD(TUFS152) .
 LIST ACDS SYSMOD(TUFS152) .
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
installed 1.5.2 new, so that `TUFS150` and the older ids were never in its zone:

```
 UCLIN CDS .
  DEL SYSMOD(TUFS152) MOD(UFSD) .
HMA2550    UPDATE COMPLETE FOR TUFS152
  DEL SYSMOD(TUFS152) MOD(UFSDSSIR) .
HMA2550    UPDATE COMPLETE FOR TUFS152
  DEL SYSMOD(TUFS152) MOD(UFSDCLNP) .
HMA2550    UPDATE COMPLETE FOR TUFS152
  DEL SYSMOD(TUFS152) MOD(UFSFMT) .
HMA2550    UPDATE COMPLETE FOR TUFS152
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
  DEL SYSMOD(TUFS152) .
HMA2550    UPDATE COMPLETE FOR TUFS152
  DEL SYSMOD(TUFS151) .
HMA2550    UPDATE COMPLETE FOR TUFS151
  DEL SYSMOD(TUFS150) .
HMA2532 ** SYSMOD ENTRY TUFS150 TO BE DELETED DOES NOT EXIST
HMA2560    UPDATE PROCESSING TERMINATED - UPDATE NOT COMPLETE
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

Then read the `LIST` as in step 4, here fourteen statements without `TUFSDEL`, and,
if you remove UFSD for good, scratch the libraries as in step 5. `UCLIN` changes
only the inventory: until you scratch them, the load modules are still in the
target library, and the copies that SMP accepted are in the distribution
library.

## You installed a development build with a later FMID

A development build of `main` is packaged under the FMID of the release that
`main` is heading for. A build of 1.6.0-dev carries `TUFS160`, although 1.6.0 is
not released. If you installed such a package by SMP, the system holds an id
that no release of this document knows. Neither the dummy function of step 2 nor
the lists of step 3 name it, so it stays in both zones, and it still owns the
load modules. Install a release only after you have cleaned it up as described
here.

A package that was built with a throwaway id of its own (`[smp] fmid` in
`mbt.toml`) is the same case with another name. Wherever this section says
`TUFS160`, read your id, and name it in the same places. A current `mbt`
refuses to package a pre-release without such an id, so a package of the first
kind comes from an older `mbt`.

Ask SMP which id owns the load module. This job lists the module in both zones:

```
//UFSDLST  JOB (SYS),'UFSD LIST',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 LIST CDS  MOD(UFSD) .
 LIST ACDS MOD(UFSD) .
/*
//
```

Read the `FMID` line of the stanza (the stanzas are shown in
[step 6](#6-check-the-result)). UFSD has used these ids, and no others: `TUFS110`, `TUFS120`, `TUFS130`, `TUFS140`, `TUFS150`, `TUFS151` and
`TUFS152`. If the `FMID` is one of them, the system belongs to the main way
above and this section is not for you. If it is any other id, such as
`TUFS160`, you are in the right section.

The steps are those of the main way, with two differences. `TUFS160` is named in
every list, and the libraries stay.

### 1. Stop the server

```
/P UFSD
```

If common storage is still held afterwards, run `/S UFSDCLNP` before you go on,
as in step 1 above.

### 2. Delete the development build with the dummy function

Submit the job of [step 2](#2-delete-the-release-with-the-dummy-function) with
one change: `TUFS160` is added to the `DELETE` operand, as the last id on the
second line of the `++VER` card. Everything else, the `HMASMP.LINKLIB` and
`HMASMP.AUFSDLOD` statements, the `COND` on the later steps and the member list
at the end, stays as it is there.

Keep the card on two lines. With the eight ids on one line it is 85 characters
long, and SMP reads columns 1 to 72 and nothing after them (see the third point
of step 2). The job of step 2 breaks the card after `TUFS150,`; the second line,
with `TUFS160` on it, is 29 characters long.

```
//UFSDDEL  JOB (SYS),'UFSD DELETE',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//RECV    EXEC SMPREC
//SMPPTFIN DD  *
++FUNCTION(TUFSDEL) .
++VER(Z038) DELETE(TUFS110,TUFS120,TUFS130,TUFS140,TUFS150,
   TUFS151,TUFS152,TUFS160) .
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

Read the three SMP steps as in step 2. The receive ends with return code 04 and
echoes the card with `TUFS160` on its second line:

```
 RECEIVE SELECT(TUFSDEL) .
----------++FUNCTION(TUFSDEL) .
----------++VER(Z038) DELETE(TUFS110,TUFS120,TUFS130,TUFS140,TUFS150,
----------   TUFS151,TUFS152,TUFS160) .
HMA3971    SYSMOD TUFSDEL HAS NO ELEMENTS
HMA3930    SYSMOD TUFSDEL SUCCESSFULLY RECEIVED
HMA2050    RECEIVE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

The apply deletes the four load modules and ends with return code 04. The
`HMA2461` line names `TUFS160`, the id that the system had applied, and not
`TUFS152` as in step 2:

```
 APPLY S(TUFSDEL) DIS(WRITE) .
HMA2240    SUCCESSFULLY DELETED LMOD UFSD ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSDCLNP ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSDSSIR ON LINKLIB LIBRARY
HMA2240    SUCCESSFULLY DELETED LMOD UFSFMT ON LINKLIB LIBRARY
HMA2270    APPLY PROCESSING SUCCESSFULLY COMPLETED FOR SYSMOD TUFSDEL
HMA2461    SYSMOD TUFS160 NOT FOUND ON SMPSCDS LIBRARY
HMA2050    APPLY PROCESSING COMPLETED - HIGHEST RETURN CODE IS 04
```

The accept prints the four `HMA2240` lines for `AUFSDLOD` as in step 2 and ends
with return code 00. Both member lists must be empty:

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

**Check this step before you go on.** If the `RECEIVE` ended with 08, the job
removed nothing: the member lists of its last step still name the four load
modules in both libraries. Do not run step 3 then, because `UCLIN` changes only
the inventory and leaves the load modules where they are. Fix the card and run
this step again. Go on when the `APPLY` and the `ACCEPT` have printed their
`HMA2240` lines and both member lists are empty.

### 3. Free the ids, with `TUFS160` in the lists

Submit the job of [step 3](#3-free-the-ids) with `TUFS160` added to the `DEL`
statements of both `UCLIN` blocks and to the `LIST` statements. In the job that
was run, `TUFS160` is the second `DEL` statement of each block, right after
`TUFSDEL` and ahead of `TUFS152`. In the `LIST` step it follows `TUFSDEL` in the
same way (`LIST CDS` for `TUFSDEL` and `TUFS160`, then `LIST ACDS` for both),
and the seven pairs for `TUFS152` down to `TUFS110` come after. Each zone now
takes nine `DEL` statements, `TUFSDEL` and the eight ids, and the `LIST` step
has eighteen statements.

Both `UCLIN` steps end with return code 00, and all nine `DEL` statements of
each report `HMA2550 UPDATE COMPLETE`. The second zone needs no statement
beyond these. The step `UCLCDS` begins like this:

```
 UCLIN CDS .
  DEL SYSMOD(TUFSDEL) .
HMA2550    UPDATE COMPLETE FOR TUFSDEL
  DEL SYSMOD(TUFS160) .
HMA2550    UPDATE COMPLETE FOR TUFS160
  DEL SYSMOD(TUFS152) .
HMA2550    UPDATE COMPLETE FOR TUFS152
```

Step `UCLACDS` gives the same. Any other message is not expected.

Read the `LIST` as in [step 4](#4-read-the-list--this-is-the-actual-result):
all eighteen statements must show their id as not found, and the step ends with
return code 04. A stanza with `TYPE = FUNCTION` for `TUFS160` is an entry that
is still there, in whichever zone it shows.

### 4. Do not scratch the libraries

Leave out step 5. The release installs into `UFSD.LINKLIB` and
`UFSD.AUFSDLOD`, which are empty now.

### 5. Install the release

Run step 5 of [installation.md](installation.md) with the install job of the
release package, which you upload as in step 3 there. **Do not run the
allocation job of step 4.** It allocates both libraries `NEW`, and they exist.
That the job fails on them has not been measured: the test runs skipped it.

The `RECEIVE`, the `APPLY CHECK`, the `APPLY` and the `ACCEPT` end with return
code 00. The `APPLY` copies each of the four modules into `UFSD.LINKLIB`, and
the `ACCEPT` into `UFSD.AUFSDLOD`. There is no `HMA2461` line. These are the
lines of the `APPLY`:

```
HMA2380    COPY SUCCESSFUL - MOD=UFSD - LMOD=UFSD - LIBRARY=LINKLIB - SYSMOD=TUFS152 - RETURN CODE=00
HMA2380    COPY SUCCESSFUL - MOD=UFSDCLNP - LMOD=UFSDCLNP - LIBRARY=LINKLIB - SYSMOD=TUFS152 - RETURN CODE=00
HMA2380    COPY SUCCESSFUL - MOD=UFSDSSIR - LMOD=UFSDSSIR - LIBRARY=LINKLIB - SYSMOD=TUFS152 - RETURN CODE=00
HMA2380    COPY SUCCESSFUL - MOD=UFSFMT - LMOD=UFSFMT - LIBRARY=LINKLIB - SYSMOD=TUFS152 - RETURN CODE=00
```

The `ACCEPT` prints the same four lines with `LIBRARY=AUFSDLOD`.

### 6. Check the result

List the members of `UFSD.LINKLIB` and `UFSD.AUFSDLOD`: the four load modules
must be in both. Then `LIST CDS MOD(UFSD) .` must show the `FMID` of the
release you installed, and not `TUFS160`. Start UFSD and read the banner; it
must name the release, and must not end in `-DEV`:

```
UFSD000I UFSD 1.5.2 (xxxxxxx) STARTING
```

A banner with `-DEV` after a clean install means that the server was loaded from
another library. A development build is deployed to `UFSD.DEV.LINKLIB`. If the
`STEPLIB` of your procedure names that library ahead of `UFSD.LINKLIB` (the
sample procedure of the release names `UFSD.LINKLIB` alone, so this is a change
you made), the server loads the copy there. The SMP steps above act on
`UFSD.LINKLIB` alone, so they leave that copy where it is. Delete the library,
or take it out of the `STEPLIB`, and start the server again. This has not been
measured for UFSD; it was measured on another product built the same way.

Both libraries list `UFSD`, `UFSDCLNP`, `UFSDSSIR` and `UFSFMT`. The two `LIST`
statements show the release, in both zones, as the owner of the module:

```
 LIST CDS MOD(UFSD) .
SMPCDS  MODULE ENTRIES
  NAME
UFSD      LASTUPD         = TUFS152  TYPE=ADD
          LIBRARIES       = DISTLIB=AUFSDLOD
          FMID            = TUFS152
          RMID            = TUFS152
          LMOD            = UFSD
 LIST ACDS MOD(UFSD) .
SMPACDS MODULE ENTRIES
  NAME
UFSD      LASTUPD         = TUFS152  TYPE=ADD
          LIBRARIES       = DISTLIB=AUFSDLOD
          FMID            = TUFS152
          RMID            = TUFS152
```

`TUFS160` is not found in either zone, and neither are `TUFS150`, `TUFS140` and
`TUFSDEL`. `TUFS151` is there with `DELBY = TUFS152`: that is the mark which the
release leaves for its predecessor, as described at the top of this document,
and not a rest of the development build. The banner of the started server names the release:

```
UFSD000I UFSD 1.5.2 (xxxxxxx) STARTING
```

**What happens without this section.** The 1.5.2 install job ends with return
code 00 in all its steps, and prints no warning. In the `APPLY CHECK`, the
`APPLY` and the `ACCEPT`, each of the four modules is `NOT SEL`: `TUFS160` still
owns it, and `TUFS152` is installed but owns nothing. Both libraries are
unchanged, `LIST CDS MOD(UFSD)` and `LIST ACDS MOD(UFSD)` still show `TUFS160`
as `FMID` and `RMID`, and the server starts as the development build:

```
UFSD000I UFSD 1.6.0-DEV (xxxxxxx) STARTING
```

The return codes do not tell you this. The banner and the `FMID` do.

**What has been measured.** The section was followed on two test systems, an
MVS/CE and a TK5, each with a development build of release 1.6.0 installed
under `TUFS160` by SMP, and the release 1.5.2 (the released one, not a
candidate) installed afterwards as above. The control run, without the section,
was made on both as well. The two systems did not start from the same state:

- On the MVS/CE system the `APPLY` of the development build ended without
  `HMA2461`. A `LIST` before the install found none of the ids and no UFSD
  module in either zone. The system image ran a UFSD of its own outside SMP,
  which was stopped first.
- On the TK5 system the development build was installed over release 1.4.0. Its
  `APPLY` ended with return code 04 and `HMA2461 SYSMOD TUFS140 NOT FOUND ON
  SMPSCDS LIBRARY`, so `TUFS140` was in the history there and went with the
  lists.

Both runs gave the results shown above.

**What has not been measured.** A system that really went through the earlier
releases, a second development build installed over the first (`TUFS160`, then
a later id), a throwaway id of your own instead of `TUFS160`, a development
build installed over a system that already holds release 1.5.2, the removal
without a release afterwards (see below), and a copy in `UFSD.DEV.LINKLIB`.

### If you want to remove UFSD and not install a release

Steps 1 to 3 above clear the development build from both zones and both
libraries. UFSD is then gone from SMP, apart from the two empty libraries, which
step 5 of the main way [scratches](#5-scratch-the-libraries). That combination,
steps 1 to 3 of this section followed by step 5 of the main way, has not been
run. Check it as you check the main way: the libraries are gone, and `LIST CDS
MOD(UFSD)` and `LIST ACDS MOD(UFSD)` find nothing.

## What is not removed, because SMP never owned it

- The procedures and the member of the configuration that you copied into the
  procedure library and the parameter library of the system.
- The entry for `UFSD.LINKLIB` in the APF list, if you made one.
- **`UFSD.DEV.LINKLIB`**, if you deployed a development build there. SMP never
  wrote it, and neither the steps above nor the install of a release clear it.
- **Your file systems.** The data sets that `ROOT` and `MOUNT` name in the
  member hold your files. None of the steps above touches them.

Those are yours to delete.

---

## Why `RESTORE` and `REJECT` do not work

Worth knowing, because the messages point away from the cause. The install job
accepts the FMID in the same run in which it applies it. That closes both of the
usual ways back:

```
HMA2452 ** SYSMOD TUFS152 SELECTED FOR RESTORE HAS BEEN ACCEPTED
HMA2462 ** SYSMOD TUFS152 NOT FOUND ON SMPPTS LIBRARY
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
