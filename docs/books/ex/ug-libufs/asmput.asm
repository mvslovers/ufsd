*        int asmput(UFS *ufs, const char *path, const char *text)
*        Writes TEXT to the file PATH through libufs.  Returns 0, or
*        the UFSD return code of the open that failed.
         CSECT
         ENTRY ASMPUT
ASMPUT   DS    0H
         STM   14,12,12(13)       save the caller's registers
         LR    12,15
         USING ASMPUT,12
         L     15,76(,13)         take a frame from the C stack
         ST    13,4(,15)
         ST    15,8(,13)
         LR    13,15
         LA    15,112(,15)
         ST    15,76(,13)         NAB past the frame
*
         LM    2,4,0(1)           R2 = ufs, R3 = path, R4 = text
*
         ST    2,88(,13)          ufs_fopen(ufs, path, "w"):
         ST    3,92(,13)          the parameter list holds the values
         LA    0,MODEW
         ST    0,96(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FOPN)
         BALR  14,15
         LTR   5,15               R5 = the file, NULL if it failed
         BZ    FAILED
*
         ST    4,88(,13)          ufs_fputs(text, file)
         ST    5,92(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FPTS)
         BALR  14,15
*
         ST    5,100(,13)         ufs_fclose(&file): a pointer to
         LA    0,100(,13)         the word that holds the file
         ST    0,88(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FCLS)
         BALR  14,15
         SR    15,15
         B     DONE
*
FAILED   ST    2,88(,13)          ufs_last_rc(ufs)
         LA    1,88(,13)
         L     15,=V(UFS#GLRC)
         BALR  14,15
*
DONE     L     13,4(,13)          release the frame
         L     14,12(,13)
         LM    0,12,20(13)      restore all but R15
         BR    14
MODEW    DC    C'w',X'00'
         LTORG
         END
