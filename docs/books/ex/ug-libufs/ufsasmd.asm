*        UFSASMD - a program whose main() is written in assembler.
*        The C startup (@@CRT0) calls it like a C main(argc, argv):
*        C linkage, C stack, return code in R15.
UFSASMD  CSECT
*
*        int main(int argc, char **argv)
         ENTRY MAIN
MAIN     DS    0H
         STM   14,12,12(13)
         LR    12,15
         USING MAIN,12
         L     15,76(,13)
         ST    13,4(,15)
         ST    15,8(,13)
         LR    13,15
         LA    15,256(,15)
         ST    15,76(,13)
*        frame: 88 outgoing list, 104 UFS, 108 FILE, 112 line buffer
         L     2,4(,1)            argv
         L     3,0(,2)            argv[0]
         ST    3,88(,13)
         LA    1,88(,13)
         L     15,=V(@@APFSET)
         BALR  14,15
         LTR   15,15
         BNZ   NOAUTH
         SR    1,1
         L     15,=V(UFSNEW)
         BALR  14,15
         LTR   5,15
         BZ    NOSESS
         ST    5,104(,13)
         ST    5,88(,13)
         LA    0,DIRNAME
         ST    0,92(,13)
         LA    1,88(,13)
         L     15,=V(UFS#MD)
         BALR  14,15
         ST    5,88(,13)
         LA    0,FILENAME
         ST    0,92(,13)
         LA    0,MODEW
         ST    0,96(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FOPN)
         BALR  14,15
         LTR   6,15
         BZ    NOOPEN
         LA    0,TEXT
         ST    0,88(,13)
         ST    6,92(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FPTS)
         BALR  14,15
         ST    6,108(,13)
         LA    0,108(,13)
         ST    0,88(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FCLS)
         BALR  14,15
         ST    5,88(,13)
         LA    0,FILENAME
         ST    0,92(,13)
         LA    0,MODER
         ST    0,96(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FOPN)
         BALR  14,15
         LTR   6,15
         BZ    NOOPEN
         LA    0,112(,13)
         ST    0,88(,13)
         LA    0,80
         ST    0,92(,13)
         ST    6,96(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FGTS)
         BALR  14,15
         LA    0,112(,13)
         ST    0,88(,13)
         LA    1,88(,13)
         L     15,=V(PUTS)
         BALR  14,15
         ST    6,108(,13)
         LA    0,108(,13)
         ST    0,88(,13)
         LA    1,88(,13)
         L     15,=V(UFS#FCLS)
         BALR  14,15
         ST    5,88(,13)
         LA    0,FILENAME
         ST    0,92(,13)
         LA    1,88(,13)
         L     15,=V(UFS#REM)
         BALR  14,15
         ST    5,88(,13)
         LA    0,DIRNAME
         ST    0,92(,13)
         LA    1,88(,13)
         L     15,=V(UFS#RD)
         BALR  14,15
         LA    0,104(,13)
         ST    0,88(,13)
         LA    1,88(,13)
         L     15,=V(UFSFREE)
         BALR  14,15
         SR    15,15
         B     EXIT
NOAUTH   LA    0,MSGAUTH
         LA    2,12
         B     SAY
NOSESS   LA    0,MSGSESS
         LA    2,12
         B     SAY
NOOPEN   LA    0,MSGOPEN
         LA    2,8
SAY      ST    0,88(,13)
         LA    1,88(,13)
         L     15,=V(PUTS)
         BALR  14,15
         LR    15,2
EXIT     L     13,4(,13)
         L     14,12(,13)
         LM    0,12,20(13)
         BR    14
DIRNAME  DC    C'/tmp/ufsasm',X'00'
FILENAME DC    C'/tmp/ufsasm/hello.txt',X'00'
MODEW    DC    C'w',X'00'
MODER    DC    C'r',X'00'
TEXT     DC    C'Hello from assembler MAIN',X'00'
MSGAUTH  DC    C'not authorized: cannot call UFSD',X'00'
MSGSESS  DC    C'ufsnew failed: is UFSD running?',X'00'
MSGOPEN  DC    C'ufs_fopen failed',X'00'
         LTORG
         END
