//UFSDACT  JOB (ACCT),'ACTIVATE UFSD',CLASS=A,MSGCLASS=H,
//         NOTIFY=&SYSUID
//*
//* Compress the UFSD STEPLIB and copy freshly deployed UFSD load
//* modules into it.
//*
//* WHY THIS EXISTS
//*   `make deploy` RECEIVEs into the deploy library built from
//*   MBT_MVS_HLQ in .env -- by default
//*   <hlq>.UFSD.<vrm>.LINKLIB -- and stops there. The started task
//*   runs from a different data set (its STEPLIB DD), so a deploy
//*   on its own changes nothing that is running.
//*
//*   There is no hot activation. UFSD is the running program
//*   itself, and it __loadhi's UFSDSSIR into CSA at startup --
//*   both only change on a stop/start cycle.
//*
//* BEFORE YOU SUBMIT
//*   Both steps use DISP=OLD, because IEBCOPY COMPRESS cannot run
//*   against a library UFSD holds SHR through its STEPLIB. The job
//*   therefore WAITS in the enqueue until UFSD releases it. Submit
//*   this first, then stop UFSD -- it starts the moment the STC
//*   ends. Note that a waiting job occupies an initiator.
//*
//*     <submit this job>
//*     P UFSD           (3270 / console)
//*     <job runs>
//*     S UFSD
//*
//*   Use P, not C. A clean /P UFSD frees all CSA; after /C or an
//*   abend the recovery handler deliberately retains it, and
//*   UFSDCLNP has to run before UFSD starts again:
//*
//*     S UFSDCLNP
//*     S UFSD
//*
//*   Every UFS client loses its filesystem while UFSD is down --
//*   ftpd and httpd answer UFS requests with "service not
//*   available" until it is back.
//*
//* CHECK THE NAMES
//*   Both data sets below are installation-specific. The load
//*   library is whatever you restored the XMIT to at install time
//*   -- docs/installation.md uses UFSD.V1R0M0.LINKLIB as its
//*   example, this stand runs UFSD.LINKLIB. Read the PROC rather
//*   than assuming either:
//*
//*     SYS2.PROCLIB(UFSD)  ->  //STEPLIB DD DSN=...
//*
//*   The deploy library is the one `make deploy` prints as its
//*   target.
//*
//* AFTERWARDS
//*   Confirm the new module is really the one running -- a deploy
//*   to the wrong library fails silently.
//*
//*   Do NOT trust the version in UFSD001I for that. It comes from
//*   -DVERSION, and bumping VERSION invalidates no object file, so
//*   an incremental build keeps whatever string ufsd.o was built
//*   with and the banner names the old version. Build clean after
//*   a bump, or look for a string only the new build has.
//*
//*   IEBCOPY prints IEB144I with the tracks left, which is the
//*   early warning for the next SE37.
//*
//COMPRESS EXEC PGM=IEBCOPY
//SYSPRINT DD SYSOUT=*
//LIB      DD DSN=UFSD.LINKLIB,DISP=OLD
//SYSIN    DD *
  COPY INDD=LIB,OUTDD=LIB
/*
//ACTIVATE EXEC PGM=IEBCOPY
//SYSPRINT DD SYSOUT=*
//IN       DD DSN=IBMUSER.UFSD.V1R1M0D.LINKLIB,DISP=SHR
//OUT      DD DSN=UFSD.LINKLIB,DISP=OLD
//SYSIN    DD *
  COPY INDD=((IN,R)),OUTDD=OUT
  SELECT MEMBER=UFSD
  SELECT MEMBER=UFSDSSIR
  SELECT MEMBER=UFSDCLNP
/*
