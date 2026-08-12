/* UFSD#MNT.C - Mount point table (issue #52)
**
** The index arithmetic behind the mount crossing in do_dirread.
** Portable C: no MVS headers, no disk I/O, no EBCDIC -- see
** include/ufsdmnt.h for why, and test/mvs/tstufsmp.c for the tests
** `make test-host` runs on it natively.
*/

#include "ufsdmnt.h"

int
ufsd_mount_child(const UFSD_MOUNTPT *tab, unsigned ntab,
                 int pdisk, unsigned ino)
{
    unsigned i;

    if (!tab || ntab == 0U || ino == 0U || pdisk < 0)
        return -1;

    for (i = 0; i < ntab; i++) {
        /* pdisk is compared first: an entry with no parent carries
        ** ino 0 as well, but only the pair proves a mount point. */
        if (tab[i].pdisk == pdisk && tab[i].ino == ino)
            return (int)i;
    }

    return -1;
}

void
ufsd_mount_remove(UFSD_MOUNTPT *tab, unsigned ntab, unsigned removed)
{
    unsigned i;

    if (!tab || removed >= ntab) return;

    /* Correct the parent indices while the array is still in the old
    ** numbering -- afterwards there is no way to tell which disk an
    ** index used to name. */
    for (i = 0; i < ntab; i++) {
        if (i == removed) continue;

        if (tab[i].pdisk == (int)removed) {
            /* Parent unmounted: the mount point directory went with
            ** it (see include/ufsdmnt.h). */
            tab[i].pdisk = UFSD_MNT_NOPARENT;
            tab[i].ino   = 0U;
            tab[i].pino  = 0U;
        } else if (tab[i].pdisk > (int)removed) {
            tab[i].pdisk--;
        }
    }

    for (i = removed; i + 1U < ntab; i++)
        tab[i] = tab[i + 1U];

    tab[ntab - 1U].pdisk = UFSD_MNT_NOPARENT;
    tab[ntab - 1U].ino   = 0U;
    tab[ntab - 1U].pino  = 0U;
}
