#include <stdio.h>
#include <mvs/apf.h>
#include "libufs.h"
int main(int argc, char **argv)
{
    UFS *ufs; UFSFILE *fp; UFSDLIST st; int rc;
    (void)argc;
    if (clib_apf_setup(argv[0]) != 0) return 12;
    ufs = ufsnew();
    if (!ufs) return 12;
    ufs_mkdir(ufs, "/tmp/ufsrel");
    rc = ufs_chgdir(ufs, "/tmp/ufsrel");
    printf("chgdir rc=%d, cwd=%s\n", rc, ufs_get_cwd(ufs)->path);
    fp = ufs_fopen(ufs, "rel.txt", "w");
    printf("fopen rel.txt w: %s\n", fp ? "ok" : "NULL");
    if (fp) { ufs_fputs("x\n", fp); ufs_fclose(&fp); }
    rc = ufs_stat(ufs, "/tmp/ufsrel/rel.txt", &st);
    printf("stat absolute rc=%d size=%u\n", rc, st.filesize);
    rc = ufs_mkdir(ufs, "sub");
    printf("mkdir sub rc=%d\n", rc);
    rc = ufs_chgdir(ufs, "..");
    printf("chgdir .. rc=%d, cwd=%s\n", rc, ufs_get_cwd(ufs)->path);
    rc = ufs_stat(ufs, "ufsrel/sub", &st);
    printf("stat ufsrel/sub rc=%d\n", rc);
    ufs_remove(ufs, "ufsrel/rel.txt");
    ufs_rmdir(ufs, "ufsrel/sub");
    rc = ufs_chgdir(ufs, "/");
    ufs_rmdir(ufs, "/tmp/ufsrel");
    printf("cleanup rmdir rc=%d\n", ufs_last_rc(ufs));
    ufsfree(&ufs);
    return 0;
}
