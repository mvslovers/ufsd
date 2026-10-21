/* UFSPIT - behaviour of libufs that differs from stdio. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <mvs/apf.h>
#include "libufs.h"

#define D "/tmp/ufspit"

static void show(UFS *ufs, const char *what)
{
    printf("%-34s last_rc=%d\n", what, ufs_last_rc(ufs));
}

int main(int argc, char **argv)
{
    UFS      *ufs;
    UFSFILE  *fp;
    UFSDDESC *dd;
    UFSDLIST  st;
    char      buf[64];
    int       n;
    int       c;

    (void)argc;
    if (clib_apf_setup(argv[0]) != 0) return 12;
    ufs = ufsnew();
    if (!ufs) return 12;
    ufs_mkdir(ufs, D);

    /* 1. mode "a" is not append */
    fp = ufs_fopen(ufs, D "/a.txt", "w");
    ufs_fputs("first\n", fp);
    ufs_fclose(&fp);
    fp = ufs_fopen(ufs, D "/a.txt", "a");
    ufs_fputs("second\n", fp);
    ufs_fclose(&fp);
    ufs_stat(ufs, D "/a.txt", &st);
    printf("after w + a: %u bytes (\"first\\n\" + \"second\\n\" = 13)\n",
           st.filesize);

    /* 2. a failed diropen leaves last_rc alone */
    ufs_mkdir(ufs, D);                       /* sets last_rc = EXIST (32) */
    show(ufs, "mkdir existing dir");
    dd = ufs_diropen(ufs, D "/nodir", NULL);
    printf("diropen missing dir: %s\n", dd ? "handle" : "NULL");
    show(ufs, "after failed diropen");
    ufs_fopen(ufs, D "/nodir/x", "r");
    show(ufs, "after failed fopen");

    /* 3. feof turns true before the last line has been read */
    fp = ufs_fopen(ufs, D "/b.txt", "w");
    ufs_fputs("one\ntwo\nthree\n", fp);
    ufs_fclose(&fp);
    fp = ufs_fopen(ufs, D "/b.txt", "r");
    n = 0;
    while (!ufs_feof(fp)) {
        if (!ufs_fgets(buf, sizeof(buf), fp)) break;
        n++;
    }
    printf("while(!feof) read %d of 3 lines\n", n);
    ufs_fclose(&fp);
    fp = ufs_fopen(ufs, D "/b.txt", "r");
    n = 0;
    while (ufs_fgets(buf, sizeof(buf), fp)) n++;
    printf("while(fgets)  read %d of 3 lines\n", n);
    ufs_fclose(&fp);

    /* 4. fgets read-ahead, then fread */
    fp = ufs_fopen(ufs, D "/b.txt", "r");
    ufs_fgets(buf, sizeof(buf), fp);
    n = (int)ufs_fread(buf, 1, 4, fp);
    printf("fgets, then fread of 4 bytes: got %d\n", n);
    ufs_fclose(&fp);

    /* 5. write on a read handle, read on a write handle */
    fp = ufs_fopen(ufs, D "/b.txt", "r");
    n = (int)ufs_fwrite("x", 1, 1, fp);
    printf("fwrite on \"r\" handle: %d, ferror=%d\n", n, ufs_ferror(fp));
    ufs_fclose(&fp);
    fp = ufs_fopen(ufs, D "/c.txt", "w");
    n = (int)ufs_fread(buf, 1, 4, fp);
    printf("fread on \"w\" handle: %d, ferror=%d\n", n, ufs_ferror(fp));
    ufs_fclose(&fp);

    /* 6. seek */
    fp = ufs_fopen(ufs, D "/b.txt", "r");
    printf("fseek: %d\n", ufs_fseek(fp, 4, UFS_SEEK_SET));
    ufs_fclose(&fp);

    /* 7. pattern is ignored */
    fp = ufs_fopen(ufs, D "/d.dat", "w");
    ufs_fclose(&fp);
    dd = ufs_diropen(ufs, D, "*.txt");
    n = 0;
    while (ufs_dirread(dd)) n++;
    printf("diropen pattern \"*.txt\": %d entries\n", n);
    ufs_dirclose(&dd);

    /* 7b. write order: fputs is buffered, fwrite is not */
    fp = ufs_fopen(ufs, D "/e.txt", "w");
    ufs_fputs("AAA", fp);
    ufs_fwrite("BBB", 1, 3, fp);
    ufs_fputs("CCC", fp);
    ufs_fclose(&fp);
    fp = ufs_fopen(ufs, D "/e.txt", "r");
    n = (int)ufs_fread(buf, 1, 9, fp);
    buf[n] = 0;
    printf("fputs AAA, fwrite BBB, fputs CCC: file holds \"%s\"\n", buf);
    ufs_fclose(&fp);

    /* 8. fopen on a directory; remove of a directory; rmdir non-empty */
    fp = ufs_fopen(ufs, D, "r");
    show(ufs, fp ? "fopen dir (unexpected)" : "fopen r on a directory");
    ufs_remove(ufs, D);
    show(ufs, "remove on a directory");
    ufs_rmdir(ufs, D);
    show(ufs, "rmdir on a non-empty directory");
    ufs_chgdir(ufs, D "/b.txt");
    show(ufs, "chgdir to a file");
    ufs_stat(ufs, D "/nothing", &st);
    show(ufs, "stat on a missing path");

    /* clean up */
    ufs_remove(ufs, D "/a.txt");
    ufs_remove(ufs, D "/b.txt");
    ufs_remove(ufs, D "/c.txt");
    ufs_remove(ufs, D "/d.dat");
    ufs_remove(ufs, D "/e.txt");
    n = ufs_rmdir(ufs, D);
    printf("cleanup rmdir rc=%d\n", n);
    ufsfree(&ufs);
    (void)c;
    return 0;
}
