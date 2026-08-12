/* LIBUFSTST.C - libufs API Integration Test
**
** AP-1f: End-to-end test of the libufs client stub library.
** Exercises the same operations as UFSDTEST but via the libufs
** UFS-compatible API instead of raw UFSSSOB calls.
**
** Test sequence:
**   1. ufsnew()             -- open UFSD session, get UFS *
**  1a. ufs_stat("/")        -- stat root, verify directory
**  1b. ufs_stat(noexist)   -- stat non-existent, verify NOFILE
**   2. ufs_mkdir()          -- create /libufstest
**  2a. ufs_stat(dir)       -- stat new directory, verify attrs
**   3. ufs_fopen() write    -- create /libufstest/hello.txt
**   4. ufs_fwrite()         -- write "Hello from libufs!"
**   5. ufs_fclose() write
**  5a. ufs_stat(file)      -- stat file, verify size + type
**   6. ufs_fopen() read     -- open for reading
**   7. ufs_fread()          -- read and verify content
**   8. ufs_fclose() read
**   9. ufs_fgets()          -- open again, read one line
**  10. ufs_fclose()
**  11. ufs_remove()         -- remove file
**  12. ufs_rmdir()          -- remove directory
**  13. ufs_get_cwd()        -- verify CWD is "/"
**  14. ufsfree()            -- close session
**
** Expected output:
**   LUFTST01I Session opened, token=0x00010001
**   LUFTST02I MKDIR /LIBUFSTEST: OK
**   LUFTST03I FOPEN write: OK
**   LUFTST04I FWRITE 19 bytes: items=19
**   LUFTST05I FCLOSE write: OK
**   LUFTST06I FOPEN read: OK
**   LUFTST07I FREAD 19 bytes: items=19
**   LUFTST08I FREAD data: Hello from libufs!
**   LUFTST09I FCLOSE read: OK
**   LUFTST10I FGETS line: Hello from libufs!
**   LUFTST11I FCLOSE fgets: OK
**   LUFTST12I REMOVE /LIBUFSTEST/HELLO.TXT: OK
**   LUFTST13I RMDIR /LIBUFSTEST: OK
**   LUFTSTSTI STAT /: OK (d, ino=2)
**   LUFTSTSTI STAT /NOEXIST: NOFILE (RC=28) -- OK
**   LUFTSTSTI STAT /LIBUFSTEST: OK (d, ino=NN, name=LIBUFSTEST)
**   LUFTSTSTI STAT /LIBUFSTEST/HELLO.TXT: OK (-, ino=NN, size=18)
**   LUFTST14I CWD: /
**   LUFTST15I Session closed
*/

#include <string.h>
#include <clibos.h>
#include <clibwto.h>
#include "libufs.h"

#define TESTDIR  "/tmp/LIBUFSTEST"
#define TESTFILE "/tmp/LIBUFSTEST/HELLO.TXT"

static const char testdata[] = "Hello from libufs!";
#define TESTLEN  18U  /* strlen("Hello from libufs!") */

int
main(int argc, char **argv)
{
    UFS      *ufs;
    UFSFILE  *fp;
    UFSCWD   *cwd;
    UFSDDESC *ddesc;
    UFSDLIST  stbuf;
    char      rdbuf[64];
    char      linebuf[64];
    UINT32    items;
    int       rc;
    int       mp_fail = 0;   /* #52: listing vs stat mismatches */

    (void)argc;

    /* APF authorization required for IEFSSREQ */
    rc = clib_apf_setup(argv[0]);
    if (rc) {
        wtof("LUFTST09E APF setup failed RC=%d", rc);
        return 8;
    }

    /* ============================================================
    ** Step 1: Open session
    ** ============================================================ */
    ufs = ufsnew();
    if (!ufs) {
        wtof("LUFTST09E ufsnew() failed -- UFSD STC running?");
        return 8;
    }
    wtof("LUFTST01I Session opened, token=0x%08X", ufs->token);

    /* ============================================================
    ** Step 1a: STAT root "/"
    ** ============================================================ */
    rc = ufs_stat(ufs, "/", &stbuf);
    if (rc != 0) {
        wtof("LUFTSTSTE STAT /: FAILED RC=%d", rc);
    } else if (stbuf.attr[0] != 'd') {
        wtof("LUFTSTSTE STAT /: expected dir, got '%c'", stbuf.attr[0]);
    } else {
        wtof("LUFTSTSTI STAT /: OK (d, ino=%u)", stbuf.inode_number);
    }

    /* ============================================================
    ** Step 1b: STAT non-existent path
    ** ============================================================ */
    rc = ufs_stat(ufs, "/NOEXIST_STAT_TEST", &stbuf);
    if (rc == UFSD_RC_NOFILE) {
        wtof("LUFTSTSTI STAT /NOEXIST: NOFILE (RC=%d) -- OK", rc);
    } else {
        wtof("LUFTSTSTE STAT /NOEXIST: expected RC=%d, got RC=%d",
             UFSD_RC_NOFILE, rc);
    }

    /* ============================================================
    ** Step 1c: Write ops on non-existent paths (RO root) → NOFILE
    ** Regression test for issue #8: must return NOFILE, not ROFS.
    ** ============================================================ */
    rc = ufs_mkdir(ufs, "/NO_SUCH_PARENT/SUBDIR");
    if (rc == UFSD_RC_NOFILE && ufs_last_rc(ufs) == UFSD_RC_NOFILE) {
        wtof("LUFTSTRFI MKDIR /NO_SUCH.../SUBDIR: NOFILE (rc+last_rc) -- OK");
    } else {
        wtof("LUFTSTRFE MKDIR /NO_SUCH.../SUBDIR: rc=%d last_rc=%d (expected %d)",
             rc, ufs_last_rc(ufs), UFSD_RC_NOFILE);
    }

    {
        UFSFILE *tfp;
        tfp = ufs_fopen(ufs, "/NO_SUCH_PARENT/FILE.TXT", "w");
        if (tfp != NULL) {
            wtof("LUFTSTRFE FOPEN /NO_SUCH.../FILE.TXT: expected NULL");
            ufs_fclose(&tfp);
        } else if (ufs_last_rc(ufs) == UFSD_RC_NOFILE) {
            wtof("LUFTSTRFI FOPEN /NO_SUCH.../FILE.TXT: NOFILE -- OK");
        } else {
            wtof("LUFTSTRFE FOPEN /NO_SUCH.../FILE.TXT: expected RC=%d, got RC=%d",
                 UFSD_RC_NOFILE, ufs_last_rc(ufs));
        }
    }

    rc = ufs_remove(ufs, "/NO_SUCH_PARENT/FILE.TXT");
    if (rc == UFSD_RC_NOFILE && ufs_last_rc(ufs) == UFSD_RC_NOFILE) {
        wtof("LUFTSTRFI REMOVE /NO_SUCH.../FILE.TXT: NOFILE (rc+last_rc) -- OK");
    } else {
        wtof("LUFTSTRFE REMOVE /NO_SUCH.../FILE.TXT: rc=%d last_rc=%d (expected %d)",
             rc, ufs_last_rc(ufs), UFSD_RC_NOFILE);
    }

    rc = ufs_rmdir(ufs, "/NO_SUCH_PARENT/SUBDIR");
    if (rc == UFSD_RC_NOFILE && ufs_last_rc(ufs) == UFSD_RC_NOFILE) {
        wtof("LUFTSTRFI RMDIR /NO_SUCH.../SUBDIR: NOFILE (rc+last_rc) -- OK");
    } else {
        wtof("LUFTSTRFE RMDIR /NO_SUCH.../SUBDIR: rc=%d last_rc=%d (expected %d)",
             rc, ufs_last_rc(ufs), UFSD_RC_NOFILE);
    }

    /* ============================================================
    ** Step 1d: Write ops on valid path but RO mount → ROFS
    ** Root "/" is read-only, so writing there must return ROFS.
    ** ============================================================ */
    rc = ufs_mkdir(ufs, "/ROFS_TEST_DIR");
    if (rc == UFSD_RC_ROFS && ufs_last_rc(ufs) == UFSD_RC_ROFS) {
        wtof("LUFTSTRFI MKDIR /ROFS_TEST_DIR: ROFS (rc+last_rc) -- OK");
    } else {
        wtof("LUFTSTRFE MKDIR /ROFS_TEST_DIR: rc=%d last_rc=%d (expected %d)",
             rc, ufs_last_rc(ufs), UFSD_RC_ROFS);
    }

    /* ============================================================
    ** Step 1e: Listing and stat must agree on the same path (#52)
    **
    ** A directory entry and a stat of the very same path are two
    ** views of one inode, so they have to name one owner, one group
    ** and one inode number.  They did not at a mount point: DIRREAD
    ** reported the mount point directory on the parent disk (owner
    ** UFSD/SYS1, as mkdir_p wrote it) while STAT resolved the mount
    ** and reported the mounted filesystem's root inode.
    **
    ** Every entry of "/" is checked, not a hardcoded mount path, so
    ** the check follows whatever the running system has mounted --
    ** and covers the plain directories at the same time, where the
    ** two views must keep agreeing.
    ** ============================================================ */
    {
        UFSDDESC *mdesc;
        UFSDLIST *dl;
        UFSDLIST  sb;
        char      mpath[72];
        unsigned  checked  = 0;
        unsigned  root_ino = 0;

        if (ufs_stat(ufs, "/", &sb) == 0)
            root_ino = sb.inode_number;

        mdesc = ufs_diropen(ufs, "/", NULL);
        if (!mdesc) {
            wtof("LUFTSTMPE diropen / failed");
            mp_fail++;
        } else {
            while ((dl = ufs_dirread(mdesc)) != NULL) {
                if (strcmp(dl->name, ".") == 0) continue;
                if (strcmp(dl->name, "..") == 0) continue;
                if (strlen(dl->name) > sizeof(mpath) - 2U) continue;

                mpath[0] = '/';
                strcpy(mpath + 1, dl->name);

                rc = ufs_stat(ufs, mpath, &sb);
                if (rc != 0) {
                    wtof("LUFTSTMPE STAT %s failed RC=%d", mpath, rc);
                    mp_fail++;
                    continue;
                }

                checked++;

                if (strcmp(dl->owner, sb.owner) != 0
                    || strcmp(dl->group, sb.group) != 0
                    || dl->inode_number != sb.inode_number) {
                    wtof("LUFTSTMPE %s: LS %s/%s ino=%u BUT STAT %s/%s ino=%u",
                         mpath, dl->owner, dl->group, dl->inode_number,
                         sb.owner, sb.group, sb.inode_number);
                    mp_fail++;
                } else {
                    wtof("LUFTSTMPI %s: LS = STAT (%s/%s, ino=%u)",
                         mpath, sb.owner, sb.group, sb.inode_number);
                }

                /* ".." one level down must name "/" again, whether
                ** the directory is a plain one or the root of a
                ** mounted filesystem -- at a mounted root it is the
                ** directory CONTAINING the mount point, which lives
                ** on the parent disk. */
                if (sb.attr[0] == 'd') {
                    UFSDDESC *sub = ufs_diropen(ufs, mpath, NULL);
                    UFSDLIST *se;
                    int       seen = 0;

                    if (!sub) {
                        wtof("LUFTSTMPE diropen %s failed", mpath);
                        mp_fail++;
                    } else {
                        while ((se = ufs_dirread(sub)) != NULL) {
                            if (strcmp(se->name, "..") != 0) continue;
                            seen = 1;
                            if (se->inode_number != root_ino) {
                                wtof("LUFTSTMPE %s/..: ino=%u, expected %u",
                                     mpath, se->inode_number, root_ino);
                                mp_fail++;
                            }
                        }
                        ufs_dirclose(&sub);
                        if (!seen) {
                            wtof("LUFTSTMPE %s: no .. entry", mpath);
                            mp_fail++;
                        }
                    }
                }
            }
            ufs_dirclose(&mdesc);
            wtof("LUFTSTMPI %u entrie(s) of / checked, %d mismatch(es)",
                 checked, mp_fail);
        }
    }

    /* ============================================================
    ** Step 2: MKDIR /LIBUFSTEST
    ** ============================================================ */
    rc = ufs_mkdir(ufs, TESTDIR);
    if (rc != 0) {
        wtof("LUFTST09E MKDIR %s failed: RC=%d", TESTDIR, rc);
        goto close_sess;
    }
    wtof("LUFTST02I MKDIR %s: OK", TESTDIR);

    /* ============================================================
    ** Step 2a: STAT newly created directory
    ** ============================================================ */
    rc = ufs_stat(ufs, TESTDIR, &stbuf);
    if (rc != 0) {
        wtof("LUFTSTSTE STAT %s: FAILED RC=%d", TESTDIR, rc);
    } else if (stbuf.attr[0] != 'd') {
        wtof("LUFTSTSTE STAT %s: expected dir, got '%c'",
             TESTDIR, stbuf.attr[0]);
    } else {
        wtof("LUFTSTSTI STAT %s: OK (d, ino=%u, name=%s)",
             TESTDIR, stbuf.inode_number, stbuf.name);
    }

    /* ============================================================
    ** Step 3: FOPEN for writing
    ** ============================================================ */
    fp = ufs_fopen(ufs, TESTFILE, "w");
    if (!fp) {
        wtof("LUFTST09E ufs_fopen write failed");
        goto rmdir_test;
    }
    wtof("LUFTST03I FOPEN write: OK");

    /* ============================================================
    ** Step 4: FWRITE
    ** ============================================================ */
    items = ufs_fwrite((void *)testdata, 1, TESTLEN, fp);
    if (items != TESTLEN) {
        wtof("LUFTST09E ufs_fwrite: expected %u items, got %u",
             TESTLEN, items);
        ufs_fclose(&fp);
        goto remove_file;
    }
    wtof("LUFTST04I FWRITE %u bytes: items=%u", TESTLEN, items);

    /* ============================================================
    ** Step 5: FCLOSE write
    ** ============================================================ */
    ufs_fclose(&fp);
    wtof("LUFTST05I FCLOSE write: OK");

    /* ============================================================
    ** Step 5a: STAT the written file
    ** ============================================================ */
    rc = ufs_stat(ufs, TESTFILE, &stbuf);
    if (rc != 0) {
        wtof("LUFTSTSTE STAT %s: FAILED RC=%d", TESTFILE, rc);
    } else if (stbuf.attr[0] != '-') {
        wtof("LUFTSTSTE STAT %s: expected file, got '%c'",
             TESTFILE, stbuf.attr[0]);
    } else if (stbuf.filesize != TESTLEN) {
        wtof("LUFTSTSTE STAT %s: expected size=%u, got %u",
             TESTFILE, TESTLEN, stbuf.filesize);
    } else {
        wtof("LUFTSTSTI STAT %s: OK (-, ino=%u, size=%u, name=%s)",
             TESTFILE, stbuf.inode_number, stbuf.filesize, stbuf.name);
    }

    /* ============================================================
    ** Step 5b: DIROPEN + DIRREAD -- list /LIBUFSTEST
    ** ============================================================ */
    ddesc = ufs_diropen(ufs, TESTDIR, NULL);
    if (ddesc) {
        UFSDLIST *dl;
        while ((dl = ufs_dirread(ddesc)) != NULL) {
            wtof("LUFTSTLSI   %c %s (ino=%u, size=%u)",
                 dl->attr[0], dl->name,
                 dl->inode_number, dl->filesize);
        }
        ufs_dirclose(&ddesc);
        wtof("LUFTSTLSI LS %s: OK", TESTDIR);
    } else {
        wtof("LUFTSTLSE diropen %s failed", TESTDIR);
    }

    /* ============================================================
    ** Step 6: FOPEN for reading
    ** ============================================================ */
    fp = ufs_fopen(ufs, TESTFILE, "r");
    if (!fp) {
        wtof("LUFTST09E ufs_fopen read failed");
        goto remove_file;
    }
    wtof("LUFTST06I FOPEN read: OK");

    /* ============================================================
    ** Step 7: FREAD
    ** ============================================================ */
    memset(rdbuf, 0, sizeof(rdbuf));
    items = ufs_fread(rdbuf, 1, TESTLEN, fp);
    if (items != TESTLEN) {
        wtof("LUFTST09E ufs_fread: expected %u items, got %u",
             TESTLEN, items);
        ufs_fclose(&fp);
        goto remove_file;
    }
    wtof("LUFTST07I FREAD %u bytes: items=%u", TESTLEN, items);

    rdbuf[TESTLEN] = '\0';
    wtof("LUFTST08I FREAD data: %s", rdbuf);

    if (memcmp(rdbuf, testdata, TESTLEN) != 0)
        wtof("LUFTST09E VERIFY FAILED: data mismatch");

    /* ============================================================
    ** Step 8: FCLOSE read
    ** ============================================================ */
    ufs_fclose(&fp);
    wtof("LUFTST09I FCLOSE read: OK");

    /* ============================================================
    ** Step 9: FGETS test -- open again, read one line
    ** ============================================================ */
    fp = ufs_fopen(ufs, TESTFILE, "r");
    if (fp) {
        memset(linebuf, 0, sizeof(linebuf));
        if (ufs_fgets(linebuf, (int)sizeof(linebuf), fp)) {
            /* Strip any trailing newline */
            rc = strlen(linebuf);
            if (rc > 0 && linebuf[rc - 1] == '\n')
                linebuf[rc - 1] = '\0';
            wtof("LUFTST10I FGETS line: %s", linebuf);
        } else {
            wtof("LUFTST09E ufs_fgets returned NULL");
        }
        ufs_fclose(&fp);
        wtof("LUFTST11I FCLOSE fgets: OK");
    }

    /* ============================================================
    ** Step 10: REMOVE
    ** ============================================================ */
remove_file:
    rc = ufs_remove(ufs, TESTFILE);
    if (rc != 0)
        wtof("LUFTST09E REMOVE %s failed: RC=%d", TESTFILE, rc);
    else
        wtof("LUFTST12I REMOVE %s: OK", TESTFILE);

    /* ============================================================
    ** Step 11: RMDIR
    ** ============================================================ */
rmdir_test:
    rc = ufs_rmdir(ufs, TESTDIR);
    if (rc != 0)
        wtof("LUFTST09E RMDIR %s failed: RC=%d", TESTDIR, rc);
    else
        wtof("LUFTST13I RMDIR %s: OK", TESTDIR);

    /* ============================================================
    ** Step 12: Verify CWD
    ** ============================================================ */
    cwd = ufs_get_cwd(ufs);
    if (cwd)
        wtof("LUFTST14I CWD: %s", cwd->path);
    else
        wtof("LUFTST09E ufs_get_cwd returned NULL");

    /* ============================================================
    ** Step 13: Close session
    ** ============================================================ */
close_sess:
    ufsfree(&ufs);
    wtof("LUFTST15I Session closed");

    /* The mount view check (step 1e) is the one step whose verdict
    ** reaches the runner: it compares two answers from the server
    ** rather than reporting one, so a mismatch is a server defect and
    ** nothing about the test environment can excuse it.  The older
    ** steps keep reporting through WTO only. */
    return mp_fail ? 8 : 0;
}
