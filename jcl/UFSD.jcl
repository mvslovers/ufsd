//UFSD     PROC M=UFSDPRM0,
//            D='SYS2.PARMLIB'
//*
//* UFSD - Filesystem Server STC Procedure
//*
//* Installation:
//*   Copy to SYS2.PROCLIB(UFSD)
//*
//* Starting:    /S UFSD
//*              /S UFSD,M=UFSDPRM1       (alternate config member)
//*              /S UFSD,D='MY.PARMLIB'   (alternate parmlib dataset)
//* Commands:    /F UFSD,STATS
//*              /F UFSD,MOUNT LIST
//*              /F UFSD,HELP
//*              /F UFSD,SHUTDOWN
//* Stopping:    /P UFSD
//*
//CLEANUP  EXEC PGM=UFSDCLNP
//STEPLIB  DD  DSN=IBMUSER.UFSD.V1R0M0D.LOAD,DISP=SHR
//UFSD     EXEC PGM=UFSD,REGION=4M,TIME=1440
//STEPLIB  DD  DSN=IBMUSER.UFSD.V1R0M0D.LOAD,DISP=SHR
//SYSUDUMP DD  SYSOUT=*
//UFSDPRM  DD  DSN=&D(&M),DISP=SHR,FREE=CLOSE
