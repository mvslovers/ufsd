*        UFSASMB - a program in assembler, with no C main program.
*        It follows the OS conventions (R13 = its own save area, R1 = a
*        parameter list) and calls libufs through CCALL stubs.
UFSASMB  CSECT
         STM   14,12,12(13)
         LR    12,15
         USING UFSASMB,12
         ST    13,SAVE+4
         LA    15,SAVE
         ST    15,8(,13)
         LR    13,15
*
*        authorize the task: clib_apf_setup("UFSASMB")
         LA    1,PL1
         L     15,=V(CAPFSET)
         BALR  14,15
         LTR   15,15
         BNZ   NOAUTH
*
*        ufs = ufsnew(): no parameters, so R1 must be 0
         SR    1,1
         L     15,=V(CUFSNEW)
         BALR  14,15
         LTR   5,15               R5 = the session
         BZ    NOSESS
         ST    5,UFS
*
*        ufs_mkdir(ufs, "/tmp/ufsasm")
         ST    5,PL2
         LA    0,DIRNAME
         ST    0,PL2+4
         OI    PL2+4,X'80'
         LA    1,PL2
         L     15,=V(CMKDIR)
         BALR  14,15
*
*        file = ufs_fopen(ufs, "/tmp/ufsasm/hello.txt", "w")
         ST    5,PL3
         LA    0,FILENAME
         ST    0,PL3+4
         LA    0,MODEW
         ST    0,PL3+8
         OI    PL3+8,X'80'
         LA    1,PL3
         L     15,=V(CFOPEN)
         BALR  14,15
         LTR   6,15               R6 = the file
         BZ    NOOPEN
*
*        ufs_fputs("Hello from assembler", file)
         LA    0,TEXT
         ST    0,PL2
         ST    6,PL2+4
         OI    PL2+4,X'80'
         LA    1,PL2
         L     15,=V(CFPUTS)
         BALR  14,15
*
*        ufs_fclose(&file)
         ST    6,FILE
         LA    0,FILE
         ST    0,PL1
         OI    PL1,X'80'
         LA    1,PL1
         L     15,=V(CFCLOSE)
         BALR  14,15
*
*        file = ufs_fopen(ufs, "/tmp/ufsasm/hello.txt", "r")
         L     5,UFS
         ST    5,PL3
         LA    0,FILENAME
         ST    0,PL3+4
         LA    0,MODER
         ST    0,PL3+8
         OI    PL3+8,X'80'
         LA    1,PL3
         L     15,=V(CFOPEN)
         BALR  14,15
         LTR   6,15
         BZ    NOOPEN
*
*        ufs_fgets(buf, 80, file); puts(buf)
         LA    0,BUF
         ST    0,PL3
         LA    0,80
         ST    0,PL3+4
         ST    6,PL3+8
         OI    PL3+8,X'80'
         LA    1,PL3
         L     15,=V(CFGETS)
         BALR  14,15
         LA    0,BUF
         ST    0,PL1
         OI    PL1,X'80'
         LA    1,PL1
         L     15,=V(CPUTS)
         BALR  14,15
*
*        ufs_fclose(&file); ufs_remove(); ufs_rmdir()
         ST    6,FILE
         LA    0,FILE
         ST    0,PL1
         OI    PL1,X'80'
         LA    1,PL1
         L     15,=V(CFCLOSE)
         BALR  14,15
         L     5,UFS
         ST    5,PL2
         LA    0,FILENAME
         ST    0,PL2+4
         OI    PL2+4,X'80'
         LA    1,PL2
         L     15,=V(CREMOVE)
         BALR  14,15
         L     5,UFS
         ST    5,PL2
         LA    0,DIRNAME
         ST    0,PL2+4
         OI    PL2+4,X'80'
         LA    1,PL2
         L     15,=V(CRMDIR)
         BALR  14,15
*
*        ufsfree(&ufs)
         LA    0,UFS
         ST    0,PL1
         OI    PL1,X'80'
         LA    1,PL1
         L     15,=V(CUFSFRE)
         BALR  14,15
         SR    2,2                return code 0
         B     EXIT
*
NOAUTH   LA    0,MSGAUTH
         LA    2,12
         B     SAY
NOSESS   LA    0,MSGSESS
         LA    2,12
         B     SAY
NOOPEN   LA    0,MSGOPEN
         LA    2,8
SAY      ST    0,PL1
         OI    PL1,X'80'
         LA    1,PL1
         L     15,=V(CPUTS)
         BALR  14,15
*
*        end the C environment: flush stdout, close the files
EXIT     L     15,=V(CCTERM)
         BALR  14,15
         LR    15,2
         L     13,SAVE+4
         L     14,12(,13)
         LM    0,12,20(13)
         BR    14
*
SAVE     DC    18F'0'
UFS      DC    F'0'
FILE     DC    F'0'
PL1      DC    A(PGMNAME+X'80000000')
PL2      DC    2F'0'
PL3      DC    3F'0'
PGMNAME  DC    C'UFSASMB',X'00'
DIRNAME  DC    C'/tmp/ufsasm',X'00'
FILENAME DC    C'/tmp/ufsasm/hello.txt',X'00'
MODEW    DC    C'w',X'00'
MODER    DC    C'r',X'00'
TEXT     DC    C'Hello from assembler',X'00'
MSGAUTH  DC    C'not authorized: cannot call UFSD',X'00'
MSGSESS  DC    C'ufsnew failed: is UFSD running?',X'00'
MSGOPEN  DC    C'ufs_fopen failed',X'00'
BUF      DC    CL82' '
         LTORG
*
*        one stub for each C function, named for the OS-linkage caller
CAPFSET  CCALL @@APFSET
CUFSNEW  CCALL UFSNEW
CUFSFRE  CCALL UFSFREE
CMKDIR   CCALL UFS#MD
CRMDIR   CCALL UFS#RD
CREMOVE  CCALL UFS#REM
CFOPEN   CCALL UFS#FOPN
CFCLOSE  CCALL UFS#FCLS
CFPUTS   CCALL UFS#FPTS
CFGETS   CCALL UFS#FGTS
CPUTS    CCALL PUTS
         END
