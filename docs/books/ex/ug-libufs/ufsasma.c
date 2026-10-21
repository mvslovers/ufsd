/* UFSASMA - a C main program calls an assembler routine that calls libufs. */
#include <stdio.h>
#include <mvs/apf.h>
#include "libufs.h"

int asmput(UFS *ufs, const char *path, const char *text) asm("ASMPUT");

int main(int argc, char **argv)
{
    UFS     *ufs;
    UFSFILE *fp;
    char     line[80];
    int      rc;

    (void)argc;
    if (clib_apf_setup(argv[0]) != 0) return 12;
    ufs = ufsnew();
    if (!ufs) return 12;

    rc = asmput(ufs, "/tmp/asm.txt", "written by assembler\n");
    printf("asmput rc=%d\n", rc);

    fp = ufs_fopen(ufs, "/tmp/asm.txt", "r");
    if (fp) {
        while (ufs_fgets(line, sizeof(line), fp))
            printf("read    %s", line);
        ufs_fclose(&fp);
    }
    ufs_remove(ufs, "/tmp/asm.txt");
    ufsfree(&ufs);
    return rc;
}
