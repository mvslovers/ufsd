# Removing UFSD

This is the supported way to take UFSD back off an MVS 3.8j system and free
its FMID for a re-install. The same procedure is chapter 7, "Removing UFSD",
of the _UFSD User's Guide_ (ML05-0001).

> **If you are holding a 1.2.0 archive, its removal instructions are wrong.**
> Its `docs/installation.md` says to run `RESTORE` and then `REJECT`. Both are
> refused once the FMID has been accepted, which the install job does in the
> same run as the APPLY. Every archive from 1.2.1 on points here instead.

> **If you are upgrading rather than removing, you are in the wrong document.**
> A release deletes its predecessor as part of its own install, and step 4
> below would scratch the libraries the new release installs into. See
> [Upgrading](https://github.com/mvslovers/ufsd/blob/main/docs/installation.md#10-upgrading-from-an-earlier-release).

## What this release put on the system

| | |
|---|---|
| FMID | `TUFS150` |
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
TUFS140   TYPE            = FUNCTION
          DELBY           = TUFS150
```

A system that went from 1.2.x through 1.3.0 and 1.4.0 to 1.5.0 carries one such
mark for every step. Deleting the id of the current release and of its
predecessor clears only the newest of them. The job below therefore names
**every id UFSD has ever used** (`TUFS110`, `TUFS120`, `TUFS130`, `TUFS140` and
`TUFS150`). An id that was never on the system is reported as not existing and
raises the return code of the step to 08 (see step 2). That does no harm, and
you need not know which releases the system has seen.

If a later release adds an id, it is added to this list too.

---

## 1. Stop the server

```
/P UFSD
```

If common storage is still held afterwards, run `/S UFSDCLNP` before you go on.
See chapter 6, "Recovery After a Failure", of the _UFSD User's Guide_.

## 2. Cut the FMID out of the SMP inventory

Submit this job. It changes the two SMP data bases, the CDS (what is applied)
and the ACDS (what is accepted), and no library:

```
//UFSDUCL  JOB (SYS),'UFSD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLCDS  EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TUFS150) MOD(UFSD) .
  DEL SYSMOD(TUFS150) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS150) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS150) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL LMOD(UFSD) .
  DEL LMOD(UFSDSSIR) .
  DEL LMOD(UFSDCLNP) .
  DEL LMOD(UFSFMT) .
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
  DEL SYSMOD(TUFS150) MOD(UFSD) .
  DEL SYSMOD(TUFS150) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS150) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS150) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
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
with return code 08. That is expected for the ids your system never had, and the
statements after it are still processed. This is the output of step `UCLCDS` on
a system that had installed 1.5.0 new, so that `TUFS130` and the older ids were
never in its zone:

```
 UCLIN CDS .
  DEL SYSMOD(TUFS150) MOD(UFSD) .
HMA2550    UPDATE COMPLETE FOR TUFS150
  DEL SYSMOD(TUFS150) MOD(UFSDSSIR) .
HMA2550    UPDATE COMPLETE FOR TUFS150
  DEL SYSMOD(TUFS150) MOD(UFSDCLNP) .
HMA2550    UPDATE COMPLETE FOR TUFS150
  DEL SYSMOD(TUFS150) MOD(UFSFMT) .
HMA2550    UPDATE COMPLETE FOR TUFS150
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
  DEL SYSMOD(TUFS150) .
HMA2550    UPDATE COMPLETE FOR TUFS150
  DEL SYSMOD(TUFS140) .
HMA2550    UPDATE COMPLETE FOR TUFS140
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
leave the ACDS untouched. The `LIST` step shows that, as step 3 explains.

## 3. Read the LIST — this is the actual result

The last step of the job is the result. For every id, in both zones, it must
answer that the entry was not found. These are the first two of the ten
statements, as the step printed them:

```
 RESETRC .
 LIST CDS  SYSMOD(TUFS150) .
LIST     SELECT SUMMARY REPORT FOR SMPCDS
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE FOR PROCESSING
 TYPE        NAME
 SYSMOD      TUFS150
 LIST ACDS SYSMOD(TUFS150) .
LIST     SELECT SUMMARY REPORT FOR SMPACDS
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE FOR PROCESSING
 TYPE        NAME
 SYSMOD      TUFS150
```

The step must end with the highest return code 04. **Return code 04 together
with this answer means that the FMID is free.** Both zones count: an id gone
from one and still in the other is not free.

**Read the stanzas, not the return code of the step.** A mark that is still
there answers with return code 00, so it does not raise the return code of the
step above the 04 that you expect. The job has ten `LIST` statements, and all
ten must show the id as not found. A stanza that names `TYPE = FUNCTION` is an
entry that is still there, whether it shows the status `REC APP ACC` of the FMID
or only a `DELBY` of its successor. This is what the statements
`LIST CDS SYSMOD(TUFS150)` and `LIST CDS SYSMOD(TUFS140)` print on a system with
1.5.0 installed:

```
 LIST CDS  SYSMOD(TUFS150) .
SMPCDS   SYSMOD ENTRIES
  NAME
TUFS150   TYPE            = FUNCTION
          STATUS          = REC  APP  ACC
          FMID            = TUFS150
          JCLIN           = YES
          DATE/TIME REC   = 26.293  21:09:08
                    APP   = 26.293  21:09:25
                    ACC   = 26.293  21:09:36
          DELETE VER(001) = TUFS140
          MOD             = UFSD      UFSDCLNP  UFSDSSIR  UFSFMT

 LIST CDS  SYSMOD(TUFS140) .
SMPCDS   SYSMOD ENTRIES
  NAME
TUFS140   TYPE            = FUNCTION
          DELBY           = TUFS150
```

## 4. Scratch the libraries

`UCLIN` changes only the inventory. The load modules are still in the target
library, and the copies that SMP accepted are in the distribution library. The
allocation job of the installation creates both with `DISP=(NEW,CATLG)`, so it
would fail against either of them. Delete them in a job of your own:

```
  DELETE UFSD.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE UFSD.AUFSDLOD NONVSAM SCRATCH PURGE
```

**Do this only when you remove UFSD for good.** After an upgrade, these are the
data sets that the next release installs into; leave them. `UFSD.SAMPLIB` may
stay. The install job scratches it itself the next time.

## 5. What is not removed, because SMP never owned it

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
usual ways back.

**`RESTORE` refuses a SYSMOD that has been accepted.**

```
HMA2452 ** SYSMOD <fmid> SELECTED FOR RESTORE HAS BEEN ACCEPTED
HMA3703 ** RESTORE PROCESSING TERMINATED BECAUSE FUNCTION SYSMOD
           <fmid> FAILED
HMA2050    RESTORE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 12
```

**`REJECT` then fails for a reason that looks unrelated.**

```
HMA2462 ** SYSMOD <fmid> NOT FOUND ON SMPPTS LIBRARY
HMA2260    REJECT PROCESSING TERMINATED FOR SYSMOD <fmid>
HMA2050    REJECT PROCESSING COMPLETED - HIGHEST RETURN CODE IS 12
```

The `ACCEPT` removes the modification control statements from the `SMPPTS` data
set, and `REJECT` needs them. `UCLIN` is not a workaround: it is the only way.

## Why the install job accepts at all

The `ACCEPT` fills the distribution library, which is the base a later
`RESTORE` of a **PTF** returns to. Without it, a `RESTORE` would delete the
module rather than revert it, because there would be no previous level to go
back to. The price is what this document is about: the FMID itself becomes
permanent by documented means.
