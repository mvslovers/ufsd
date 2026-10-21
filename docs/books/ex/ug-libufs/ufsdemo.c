/* UFSDEMO - create, write, read, list and remove a file through UFSD. */
#include <stdio.h>
#include <string.h>
#include <mvs/apf.h>
#include "libufs.h"

#define DIR_NAME  "/tmp/ufsdemo"
#define FILE_NAME "/tmp/ufsdemo/hello.txt"

static int fail(UFS *ufs, const char *what)
{
    printf("FAILED  %-12s rc=%d\n", what, ufs_last_rc(ufs));
    return 8;
}

int main(int argc, char **argv)
{
    UFS      *ufs;
    UFSFILE  *fp;
    UFSDDESC *dd;
    UFSDLIST *e;
    UFSDLIST  st;
    char      line[80];
    int       rc = 0;

    (void)argc;

    /* UFSD only serves authorized callers (see the Guide). */
    if (clib_apf_setup(argv[0]) != 0) {
        printf("not authorized: cannot call UFSD\n");
        return 12;
    }

    ufs = ufsnew();
    if (!ufs) {
        printf("ufsnew failed: is UFSD running?\n");
        return 12;
    }
    printf("session open\n");

    /* Directory: an existing one is not an error for this program. */
    if (ufs_mkdir(ufs, DIR_NAME) != 0
        && ufs_last_rc(ufs) != UFSD_RC_EXIST) {
        rc = fail(ufs, "mkdir");
        goto done;
    }

    /* Create and write. "w" creates the file or truncates it. */
    fp = ufs_fopen(ufs, FILE_NAME, "w");
    if (!fp) {
        rc = fail(ufs, "fopen w");
        goto done;
    }
    ufs_fputs("Hello from libufs\n", fp);
    ufs_fputs("second line\n", fp);
    ufs_fsync(fp);
    if (ufs_ferror(fp)) {
        printf("write error %d\n", ufs_ferror(fp));
        rc = 8;
    }
    ufs_fclose(&fp);
    if (rc) goto done;

    /* Read it back. */
    fp = ufs_fopen(ufs, FILE_NAME, "r");
    if (!fp) {
        rc = fail(ufs, "fopen r");
        goto done;
    }
    while (ufs_fgets(line, sizeof(line), fp))
        printf("read    %s", line);
    ufs_fclose(&fp);

    /* List the directory. */
    dd = ufs_diropen(ufs, DIR_NAME, NULL);
    if (!dd) {
        rc = fail(ufs, "diropen");
        goto done;
    }
    while ((e = ufs_dirread(dd)) != NULL)
        printf("entry   %s %-8s %6u %s\n",
               e->attr, e->owner, e->filesize, e->name);
    ufs_dirclose(&dd);

    /* Metadata without opening. */
    if (ufs_stat(ufs, FILE_NAME, &st) == 0)
        printf("stat    %s: %u bytes, owner \"%s\"\n",
               st.name, st.filesize, st.owner);

    /* Errors: ask for something that is not there. */
    fp = ufs_fopen(ufs, DIR_NAME "/missing", "r");
    if (!fp)
        printf("expected failure: fopen r missing, rc=%d\n",
               ufs_last_rc(ufs));
    else
        ufs_fclose(&fp);

done:
    /* Clean up, then close the session. */
    ufs_remove(ufs, FILE_NAME);
    ufs_rmdir(ufs, DIR_NAME);
    ufsfree(&ufs);
    printf("session closed, rc=%d\n", rc);
    return rc;
}
