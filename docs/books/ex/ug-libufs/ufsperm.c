/* UFSPERM - read-only mounts, owner-restricted mounts, ufs_setuser. */
#include <stdio.h>
#include <mvs/apf.h>
#include "libufs.h"

static void try_mkdir(UFS *ufs, const char *path)
{
    int rc = ufs_mkdir(ufs, path);
    printf("mkdir %-18s rc=%d\n", path, rc);
}

int main(int argc, char **argv)
{
    UFS *ufs;
    int  rc;

    (void)argc;
    if (clib_apf_setup(argv[0]) != 0) return 12;
    ufs = ufsnew();
    if (!ufs) return 12;

    try_mkdir(ufs, "/newdir");            /* "/" is read-only */
    try_mkdir(ufs, "/u/herc01/ufsperm");  /* owner HERC01, session has none */

    rc = ufs_setuser(ufs, "HERC01", NULL);
    printf("setuser HERC01 rc=%d\n", rc);
    try_mkdir(ufs, "/u/herc01/ufsperm");

    rc = ufs_setuser(ufs, "MALORY", NULL);
    printf("setuser MALORY rc=%d\n", rc);
    try_mkdir(ufs, "/u/herc01/ufsperm2");

    ufs_setuser(ufs, "HERC01", NULL);
    ufs_rmdir(ufs, "/u/herc01/ufsperm");
    ufsfree(&ufs);
    return 0;
}
