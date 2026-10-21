*        UFSASMC - what does NOT work: calling ufsnew() directly
*        with OS linkage, without a CCALL stub and a C environment.
UFSASMC  CSECT
         STM   14,12,12(13)
         LR    12,15
         USING UFSASMC,12
         ST    13,SAVE+4
         LA    15,SAVE
         ST    15,8(,13)
         LR    13,15
         L     15,=V(UFSNEW)
         BALR  14,15
         L     13,SAVE+4
         L     14,12(,13)
         LM    0,12,20(13)
         SR    15,15
         BR    14
SAVE     DC    18F'0'
         LTORG
         END
