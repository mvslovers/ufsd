/* UFSNOAPF - what ufsnew() does when the caller is not authorized. */
#include <stdio.h>
#include <errno.h>
#include "libufs.h"

int main(void)
{
    UFS *ufs;

    errno = 0;
    ufs = ufsnew();
    if (!ufs) {
        printf("ufsnew returned NULL, errno=%d\n", errno);
        return 12;
    }
    printf("session open\n");
    ufsfree(&ufs);
    return 0;
}
