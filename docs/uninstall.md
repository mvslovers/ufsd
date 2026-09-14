# Removing UFSD

This is the supported way to take UFSD back off an MVS 3.8j system and free
its FMID for a re-install.

> **If you are holding a 1.2.0 archive, its removal instructions are wrong.**
> `README.md` in `ufsd-1.2.0-dist.zip` says to run `RESTORE` and then `REJECT`.
> Both are refused once the FMID has been accepted — which the install job
> does, in the same run as the APPLY. Every archive from 1.2.1 on points here
> instead.

## What this release put on the system

| | |
|---|---|
| FMID | `TUFS130` |
| Load modules | `UFSD`, `UFSDSSIR`, `UFSDCLNP`, `UFSFMT` |
| Target library | `UFSD.LINKLIB` |
| Distribution library | `UFSD.AUFSDLOD` |
| Sample library | `UFSD.SAMPLIB` |

The names carry no version qualifier (issue #71), so there is exactly one UFSD
installation on the system and these are its datasets whatever release put them
there. Releases before 1.3.0 versioned them — if ISPF 3.4 on `UFSD.*` shows
`UFSD.V1R2M2.LINKLIB` and friends, you are removing one of those: every name
below takes that qualifier back, and the FMID is `TUFS120` rather than
`TUFS130`.

One id per release since 1.3.0, so read `TUFS130` as *the release you are
removing* — 1.3.1 would be `TUFS131`.

**Its predecessors are not finished with, and there is more than one of them.**
Every upgrade deletes the level before it and leaves that id behind as a
`DELBY` tombstone in both zones, so a system that came 1.2.x → 1.3.0 → 1.3.1
carries two of them and a `DEL SYSMOD` for the release plus its immediate
predecessor clears only the newest. The job in step 2 therefore names **every
id UFSD has ever spent**, which is why it lists more than you installed — see
[Why the job names ids you never installed](#why-the-job-names-ids-you-never-installed).

The staging library `UFSD.UFSDLOAD` is not listed because the install job's
`CLEANUP` step already scratched it.

> **If you are upgrading rather than removing, you are in the wrong document.**
> Since 1.3.0 a release deletes its predecessor as part of its own install —
> there is no FMID to free by hand, and step 4 below would scratch the very
> libraries the new release installs into. See
> [Upgrading](https://github.com/mvslovers/ufsd/blob/main/docs/installation.md#10-upgrading-from-an-earlier-release).

---

## 1. Stop the server

```
/P UFSD
```

If anything is still holding CSA afterwards, run `/S UFSDCLNP` before going
on — see
[recovery.md](https://github.com/mvslovers/ufsd/blob/main/docs/recovery.md).

## 2. Cut the FMID out of the SMP inventory

Submit this. It edits the CDS and the ACDS and touches no library:

```
//UFSDUCL  JOB (SYS),'UFSD UNINSTALL',
//             CLASS=A,MSGCLASS=H,MSGLEVEL=(1,1),
//             REGION=4096K
//UCLIN   EXEC SMPAPP
//SMPCNTL  DD  *
 UCLIN CDS .
  DEL SYSMOD(TUFS130) MOD(UFSD) .
  DEL SYSMOD(TUFS130) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS130) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS130) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL LMOD(UFSD) .
  DEL LMOD(UFSDSSIR) .
  DEL LMOD(UFSDCLNP) .
  DEL LMOD(UFSFMT) .
  DEL SYSMOD(TUFS131) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
 UCLIN ACDS .
  DEL SYSMOD(TUFS130) MOD(UFSD) .
  DEL SYSMOD(TUFS130) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS130) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS130) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL SYSMOD(TUFS131) .
  DEL SYSMOD(TUFS130) .
  DEL SYSMOD(TUFS120) .
  DEL SYSMOD(TUFS110) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TUFS131) .
 LIST ACDS SYSMOD(TUFS131) .
 LIST CDS  SYSMOD(TUFS130) .
 LIST ACDS SYSMOD(TUFS130) .
 LIST CDS  SYSMOD(TUFS120) .
 LIST ACDS SYSMOD(TUFS120) .
 LIST CDS  SYSMOD(TUFS110) .
 LIST ACDS SYSMOD(TUFS110) .
/*
//
```

Every `DEL` reports `HMA2550 UPDATE COMPLETE`, and each `UCLIN` block ends
`RC 00`.

### Why the job names ids you never installed

An upgrade does not remove its predecessor from the inventory. It leaves the id
behind as a *tombstone*:

```
TUFS120   TYPE            = FUNCTION
          DELBY           = TUFS130
```

A `LIST` answers **RC 00** for that stanza, not RC 04, so the "RC 04 and an
empty list means the id is free" rule in step 3 reads it as still occupied. The
id is spent, and it stays spent until something clears it.

**Tombstones accumulate, one per upgrade.** A system that went 1.2.x → 1.3.0 →
1.3.1 holds two: `TUFS120` marked `DELBY = TUFS130`, and `TUFS130` marked
`DELBY = TUFS131`. Deleting the release and its immediate predecessor clears
the newer one and leaves the older standing for good — and because the `LIST`
in step 3 only asks about the ids you named, the job reports complete success
while a tombstone survives. That is why the block above names **every id UFSD
has ever spent** (`TUFS110`, `TUFS120`, `TUFS130`, `TUFS131`) rather than the
one you are removing plus one.

A plain `DEL SYSMOD` clears a tombstone — measured on drnmig3a 2026-09-14 by
the httpd project (`TSTHCLN JOB00043`, 29 × `HMA2550`, COND 0000, both ids gone
from both zones afterwards). An id that was never on this system reports
nothing to do, so every line is safe whether it hits or not. **That is the
whole reason the list is unconditional**: it needs no judgement from you about
which releases this system has seen, and getting that judgement wrong is
exactly the failure it prevents.

If a future release adds an id, it is added here too — the list grows one line
per release, on the same cadence as the `fmid` bump in `project.toml`.

## 3. Read the LIST — this is the actual result

The `LIST` step is what tells you whether it worked. Every id must answer, in
both zones:

```
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE
FOR PROCESSING
 TYPE        NAME
 SYSMOD      TUFS130
```

with `HIGHEST RETURN CODE IS 04`. **RC 04 and an empty list means the FMID is
free.** Both zones matter: the CDS records what is applied, the ACDS what is
accepted, and they are separate inventories — an id gone from one and present
in the other is not free.

**Read the stanzas, not the step's return code.** The `LIST` reports RC 04 for
the ids that are gone, so a single surviving tombstone — which answers RC 00 —
does not raise the step's highest return code above the 04 you were expecting.
There are eight `LIST` statements above and all eight must show the id as not
found; a stanza naming `TYPE = FUNCTION` and `DELBY` is one that is still
there.

## 4. Scratch the libraries

`UCLIN` edits the inventory only. The load modules are still in the target
library and SMP's accepted copies are still in the distribution library. The
allocation job in the install guide is a `DISP=(NEW,CATLG)` step, so it would
fail against either of them:

```
  DELETE UFSD.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE UFSD.AUFSDLOD NONVSAM SCRATCH PURGE
```

**Only when you are removing UFSD for good.** For an upgrade these are the
datasets the next release installs into; leave them.

Leave `UFSD.SAMPLIB` alone either way if you like — the install job's `DELOLD`
step scratches it on its own.

## 5. What is not removed, because SMP never owned it

- The procedures and the configuration member you copied into your PROCLIB and
  PARMLIB.
- **Your UFS disks.** The datasets named by `ROOT` and `MOUNT` in `UFSDPRM0`
  hold your files and are untouched by any of the above.

Those are yours to delete.

---

## Why `RESTORE` and `REJECT` do not work

Worth knowing, because the messages point away from the cause.

**`RESTORE` refuses an accepted SYSMOD.**

```
HMA2452 ** SYSMOD <fmid> SELECTED FOR RESTORE HAS BEEN ACCEPTED
HMA3703 ** RESTORE PROCESSING TERMINATED BECAUSE FUNCTION SYSMOD
           <fmid> FAILED
HMA2050    RESTORE PROCESSING COMPLETED - HIGHEST RETURN CODE IS 12
```

**`REJECT` then fails for an unrelated-looking reason.**

```
HMA2462 ** SYSMOD <fmid> NOT FOUND ON SMPPTS LIBRARY
HMA2260    REJECT PROCESSING TERMINATED FOR SYSMOD <fmid>
HMA2050    REJECT PROCESSING COMPLETED - HIGHEST RETURN CODE IS 12
```

The `ACCEPT` removes the modification control statements from `SYS1.SMPPTS`,
and `REJECT` works from that member. So accepting a function SYSMOD closes
both documented routes at once: `RESTORE` because it was accepted, `REJECT`
because accepting took away what it needs. `UCLIN` is not a workaround here,
it is the only way.

This was measured on 2026-08-14 against an accepted FMID installed from a
package built by this generator, on an MVS/CE system running SMP 4 level
04.48.

## Why the install job accepts at all

The `ACCEPT` fills the distribution library, which is the base a later
`RESTORE` of a **PTF** returns to. Without it, a `RESTORE` would delete the
module rather than revert it, because there would be no previous level to go
back to. The cost is what this document is about: the FMID itself becomes
permanent by documented means.
