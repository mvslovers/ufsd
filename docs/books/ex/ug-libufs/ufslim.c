/* UFSLIM - how many files a directory and a session can hold. */
#include <stdio.h>
#include <string.h>
#include <mvs/apf.h>
#include "libufs.h"

#define D "/tmp/ufslim"

int main(int argc, char **argv)
{
    UFS     *ufs;
    UFSFILE *f[70];
    UFSFILE *fp;
    char     name[200];
    int      i, n;

    (void)argc;
    if (clib_apf_setup(argv[0]) != 0) return 12;
    ufs = ufsnew();
    if (!ufs) return 12;
    ufs_mkdir(ufs, D);

    /* create files one by one, closing each at once */
    for (n = 0; n < 150; n++) {
        sprintf(name, D "/f%03d", n);
        fp = ufs_fopen(ufs, name, "w");
        if (!fp) break;
        ufs_fclose(&fp);
    }
    printf("files created in one directory: %d, then rc=%d\n",
           n, ufs_last_rc(ufs));

    /* hold files open */
    for (i = 0; i < 70; i++) {
        sprintf(name, D "/f%03d", i);
        f[i] = ufs_fopen(ufs, name, "r");
        if (!f[i]) break;
    }
    printf("files open at once in one session: %d, then rc=%d\n",
           i, ufs_last_rc(ufs));
    while (i-- > 0) ufs_fclose(&f[i]);

    for (i = 0; i < n; i++) {
        sprintf(name, D "/f%03d", i);
        ufs_remove(ufs, name);
    }
    printf("cleanup rmdir rc=%d\n", ufs_rmdir(ufs, D));
    ufsfree(&ufs);
    return 0;
}
