#include <stdio.h>
#include <errno.h>
#include <mvs/apf.h>
#include "libufs.h"
int main(int argc, char **argv)
{
    UFS *ufs;
    (void)argc;
    printf("apf_setup rc=%d\n", clib_apf_setup(argv[0]));
    errno = 0;
    ufs = ufsnew();
    printf("ufsnew %s, errno=%d\n", ufs ? "ok" : "NULL", errno);
    return ufs ? 0 : 12;
}
