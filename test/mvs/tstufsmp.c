/* TSTUFSMP.C - Mount point table tests (issue #52)
**
** Covers the table do_dirread crosses a mount with (src/ufsd#mnt.c).
** Dual-target: `make test-host` runs it natively, `make test-mvs` runs
** it as a load module.
**
** Two failure modes here are silent on a running system and both send
** a client to the wrong disk:
**
**   - matching a directory entry on its inode number alone.  Inode
**     numbers are per filesystem, so inode 5 exists on nearly every
**     disk; a lookup that ignores the parent would report a mount on
**     the first disk that happens to reuse the number, and the listing
**     would show another filesystem's root where a plain directory is.
**
**   - not moving the parent indices when UNMOUNT compacts
**     UFSD_STC.disks[].  Every disk above the removed one shifts down
**     a slot, so a parent index recorded before the unmount then names
**     a different filesystem -- the one that moved into that slot.
**
** So both are asserted from either side: a mount that must be found,
** and a near miss on the same inode number that must not.
**
** The layout below is the sample parmlib (samplib/ufsdprm0) as a
** table: a root disk, /tmp and /u/ibmuser on it, and one filesystem
** nested inside another.  Inode numbers are arbitrary but reused
** across disks on purpose -- that reuse is the point.
*/

#include <mbtcheck.h>
#include "ufsdmnt.h"

/* Disk indices, in mount order (parents before children, as
** ufsd_ufs_init sorts them). */
#define D_ROOT   0      /* /              */
#define D_TMP    1      /* /tmp           */
#define D_HOME   2      /* /u/ibmuser     */
#define D_WORK   3      /* /tmp/work      */

#define NDISKS   4U

/* Root disk inodes: /u is 7, the mount point directories are 5 and 9. */
#define INO_U        7U
#define INO_TMP      5U
#define INO_IBMUSER  9U

/* /tmp's own inodes.  Its mount point directory for /tmp/work carries
** inode 5 as well -- the same number as /tmp itself on the root disk,
** on a different filesystem. */
#define INO_WORK     5U
#define INO_ROOT     2U

/* ============================================================
** sample_table
**
** Fill `tab` with the four-disk layout described above.
** ============================================================ */
static void
sample_table(UFSD_MOUNTPT *tab)
{
    tab[D_ROOT].pdisk = UFSD_MNT_NOPARENT;   /* nothing covers "/"   */
    tab[D_ROOT].ino   = 0U;
    tab[D_ROOT].pino  = 0U;

    tab[D_TMP].pdisk  = D_ROOT;              /* /tmp on the root     */
    tab[D_TMP].ino    = INO_TMP;
    tab[D_TMP].pino   = INO_ROOT;

    tab[D_HOME].pdisk = D_ROOT;              /* /u/ibmuser, ".." = /u */
    tab[D_HOME].ino   = INO_IBMUSER;
    tab[D_HOME].pino  = INO_U;

    tab[D_WORK].pdisk = D_TMP;               /* /tmp/work, nested    */
    tab[D_WORK].ino   = INO_WORK;
    tab[D_WORK].pino  = INO_ROOT;
}

/* ============================================================
** check_lookup
**
** Every mount point is found from the disk that holds it, and only
** from that disk.
** ============================================================ */
static void
check_lookup(void)
{
    UFSD_MOUNTPT tab[NDISKS];

    sample_table(tab);

    CHECK_EQ(ufsd_mount_child(tab, NDISKS, D_ROOT, INO_TMP), D_TMP,
             "/tmp is found on the root disk");
    CHECK_EQ(ufsd_mount_child(tab, NDISKS, D_ROOT, INO_IBMUSER), D_HOME,
             "/u/ibmuser is found on the root disk");
    CHECK_EQ(ufsd_mount_child(tab, NDISKS, D_TMP, INO_WORK), D_WORK,
             "a nested mount is found on its own parent disk");

    /* INO_TMP and INO_WORK are the same number on two disks. */
    CHECK_EQ(ufsd_mount_child(tab, NDISKS, D_HOME, INO_TMP), -1,
             "the same inode number on another disk is not a mount");

    CHECK_EQ(ufsd_mount_child(tab, NDISKS, D_ROOT, INO_U), -1,
             "a plain directory carries no mount");
    CHECK_EQ(ufsd_mount_child(tab, NDISKS, D_ROOT, 0U), -1,
             "a free directory slot is never a mount");
    CHECK_EQ(ufsd_mount_child(tab, NDISKS, UFSD_MNT_NOPARENT, INO_TMP), -1,
             "no parent disk finds nothing");
    CHECK_EQ(ufsd_mount_child(NULL, NDISKS, D_ROOT, INO_TMP), -1,
             "null table finds nothing");
    CHECK_EQ(ufsd_mount_child(tab, 0U, D_ROOT, INO_TMP), -1,
             "empty table finds nothing");

    /* ndisks is the whole contract with the caller: a slot past it
    ** belongs to no mounted filesystem. */
    CHECK_EQ(ufsd_mount_child(tab, (unsigned)D_WORK, D_TMP, INO_WORK), -1,
             "an entry past ntab is not searched");
}

/* ============================================================
** check_remove_reindex
**
** Unmount /tmp, which sits below /u/ibmuser in the array.  Everything
** above it moves down one slot, and /u/ibmuser must still be found --
** at its new index.
** ============================================================ */
static void
check_remove_reindex(void)
{
    UFSD_MOUNTPT tab[NDISKS];

    sample_table(tab);

    /* Re-hang the nested mount under /u/ibmuser (disk 2) so the
    ** removal has a parent index above it to correct. */
    tab[D_WORK].pdisk = D_HOME;

    ufsd_mount_remove(tab, NDISKS, (unsigned)D_TMP);

    /* /u/ibmuser: was 2, now 1. */
    CHECK_EQ(ufsd_mount_child(tab, NDISKS - 1U, D_ROOT, INO_IBMUSER), 1,
             "a mount above the removed one is found at its new index");

    /* The nested filesystem: was 3 under parent 2, now 2 under 1. */
    CHECK_EQ(ufsd_mount_child(tab, NDISKS - 1U, 1, INO_WORK), 2,
             "a parent index above the removed one moves with it");

    CHECK_EQ(ufsd_mount_child(tab, NDISKS - 1U, D_ROOT, INO_TMP), -1,
             "the unmounted filesystem is gone from the table");

    CHECK_EQ(tab[NDISKS - 1U].pdisk, UFSD_MNT_NOPARENT,
             "the vacated slot carries no stale parent");
}

/* ============================================================
** check_remove_orphan
**
** Unmount /tmp while /tmp/work is mounted inside it.  The mount point
** directory went away with the filesystem that held it, so the nested
** entry must lose it -- keeping the inode number would cross into
** /tmp/work from whichever filesystem later occupies that index.
** ============================================================ */
static void
check_remove_orphan(void)
{
    UFSD_MOUNTPT tab[NDISKS];
    int          i;
    int          found;

    sample_table(tab);
    ufsd_mount_remove(tab, NDISKS, (unsigned)D_TMP);

    found = 0;
    for (i = 0; i < (int)(NDISKS - 1U); i++) {
        if (ufsd_mount_child(tab, NDISKS - 1U, i, INO_WORK) >= 0)
            found = 1;
    }
    CHECK(!found,
          "a mount whose parent was unmounted is reachable from no disk");

    /* The other mount on the removed disk's former parent is untouched. */
    CHECK_EQ(ufsd_mount_child(tab, NDISKS - 1U, D_ROOT, INO_IBMUSER), 1,
             "an unrelated mount survives the removal");
}

/* ============================================================
** check_remove_bounds
**
** The last entry, and an index that names no disk at all.
** ============================================================ */
static void
check_remove_bounds(void)
{
    UFSD_MOUNTPT tab[NDISKS];

    sample_table(tab);
    ufsd_mount_remove(tab, NDISKS, (unsigned)D_WORK);

    CHECK_EQ(ufsd_mount_child(tab, NDISKS - 1U, D_ROOT, INO_TMP), D_TMP,
             "removing the last entry leaves the others in place");
    CHECK_EQ(ufsd_mount_child(tab, NDISKS - 1U, D_TMP, INO_WORK), -1,
             "the removed last entry is gone");

    sample_table(tab);
    ufsd_mount_remove(tab, NDISKS, NDISKS);
    CHECK_EQ(ufsd_mount_child(tab, NDISKS, D_TMP, INO_WORK), D_WORK,
             "an out-of-range removal changes nothing");

    ufsd_mount_remove(NULL, NDISKS, (unsigned)D_TMP);
    CHECK(1, "a null table does not fault");
}

int
main(void)
{
    printf("=== UFSD mount point table tests ===\n");

    check_lookup();
    check_remove_reindex();
    check_remove_orphan();
    check_remove_bounds();

    return mbt_test_summary("TSTUFSMP");
}
