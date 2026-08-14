# Removing UFSD

This is the supported way to take UFSD back off an MVS 3.8j system and free
its FMID for a re-install.

> **The removal instructions inside the shipped release archive are wrong.**
> `README.md` in `ufsd-<version>-dist.zip` says to run `RESTORE` and then
> `REJECT`. Both are refused once the FMID has been accepted — which the
> install job does, in the same run as the APPLY. Use this document instead.

## What this release put on the system

| | |
|---|---|
| FMID | `TUFS120` |
| Load modules | `UFSD`, `UFSDSSIR`, `UFSDCLNP`, `UFSFMT` |
| Target library | `UFSD.<vrm>.LINKLIB` |
| Distribution library | `UFSD.<vrm>.AUFSDLOD` |
| Sample library | `UFSD.<vrm>.SAMPLIB` |

`<vrm>` is the release as MVS qualifier — `V1R2M0` for 1.2.x. The staging
library `UFSD.<vrm>.UFSDLOAD` is not listed because the install job's
`CLEANUP` step already scratched it.

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
  DEL SYSMOD(TUFS120) MOD(UFSD) .
  DEL SYSMOD(TUFS120) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS120) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS120) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL LMOD(UFSD) .
  DEL LMOD(UFSDSSIR) .
  DEL LMOD(UFSDCLNP) .
  DEL LMOD(UFSFMT) .
  DEL SYSMOD(TUFS120) .
 ENDUCL .
 UCLIN ACDS .
  DEL SYSMOD(TUFS120) MOD(UFSD) .
  DEL SYSMOD(TUFS120) MOD(UFSDSSIR) .
  DEL SYSMOD(TUFS120) MOD(UFSDCLNP) .
  DEL SYSMOD(TUFS120) MOD(UFSFMT) .
  DEL MOD(UFSD) .
  DEL MOD(UFSDSSIR) .
  DEL MOD(UFSDCLNP) .
  DEL MOD(UFSFMT) .
  DEL SYSMOD(TUFS120) .
 ENDUCL .
/*
//LIST    EXEC SMPAPP
//SMPCNTL  DD  *
 RESETRC .
 LIST CDS  SYSMOD(TUFS120) .
 LIST ACDS SYSMOD(TUFS120) .
/*
//
```

Every `DEL` reports `HMA2550 UPDATE COMPLETE`, and each `UCLIN` block ends
`RC 00`.

## 3. Read the LIST — this is the actual result

The `LIST` step is what tells you whether it worked. Both zones must answer:

```
THE FOLLOWING SELECTED ENTRIES WERE NOT FOUND OR WERE NOT ELIGIBLE
FOR PROCESSING
 TYPE        NAME
 SYSMOD      TUFS120
```

with `HIGHEST RETURN CODE IS 04`. **RC 04 and an empty list means the FMID is
free.** Both zones matter: the CDS records what is applied, the ACDS what is
accepted, and they are separate inventories — an id gone from one and present
in the other is not free.

## 4. Scratch the libraries

`UCLIN` edits the inventory only. The load modules are still in the target
library and SMP's accepted copies are still in the distribution library, so a
re-install would find both datasets already there and its allocation job would
fail:

```
  DELETE UFSD.<vrm>.LINKLIB  NONVSAM SCRATCH PURGE
  DELETE UFSD.<vrm>.AUFSDLOD NONVSAM SCRATCH PURGE
```

Leave `UFSD.<vrm>.SAMPLIB` alone if you like — the install job's `DELOLD` step
scratches it on its own.

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
