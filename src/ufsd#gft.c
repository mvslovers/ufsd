/* UFSD#GFT.C - Global Open File Table
**
** AP-1e: Manage the Global File Table (GFT), an array of UFSD_GFILE
** entries in STC heap memory.  The pointer is published into the CSA
** anchor so it can be read (but not written) from key-0 context.
**
** In Phase 1, refcount is always 1 (no shared file descriptors).
**
** ufsd_gft_init    (UFSD@GTI)  allocate GFT array, publish to anchor
** ufsd_gft_free    (UFSD@GTF)  free GFT array, clear anchor pointer
** ufsd_gft_alloc   (UFSD@GTA)  find a free slot, mark it USED
** ufsd_gft_release (UFSD@GTR)  clear a slot (zero it out)
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>
#include <clibos.h>
#include <clibwto.h>

/* ============================================================
** ufsd_gft_init
**
** Allocate UFSD_MAX_GFILES UFSD_GFILE entries in STC heap and
** publish the pointer + count into the CSA anchor.
** Returns 0 on success, 8 on failure.
** ============================================================ */
int
ufsd_gft_init(UFSD_ANCHOR *anchor)
{
    UFSD_GFILE    *gfiles;
    unsigned char  savekey;

    if (!anchor) return 8;

    gfiles = (UFSD_GFILE *)calloc(UFSD_MAX_GFILES, sizeof(UFSD_GFILE));
    if (!gfiles) {
        wtof("UFSD047E Cannot allocate global file table");
        return 8;
    }

    if (__super(PSWKEY0, &savekey)) {
        free(gfiles);
        wtof("UFSD048E Cannot enter supervisor state for GFT init");
        return 8;
    }
    anchor->gfiles     = gfiles;
    anchor->max_gfiles = UFSD_MAX_GFILES;
    __prob(savekey, NULL);

    return 0;
}

/* ============================================================
** ufsd_gft_free
**
** Clear the anchor GFT pointer and free the array.
** Called at shutdown before ufsd_csa_free.
** ============================================================ */
void
ufsd_gft_free(UFSD_ANCHOR *anchor)
{
    UFSD_GFILE    *gfiles;
    unsigned char  savekey;

    if (!anchor) return;

    if (__super(PSWKEY0, &savekey)) return;
    gfiles             = anchor->gfiles;
    anchor->gfiles     = NULL;
    anchor->max_gfiles = 0;
    __prob(savekey, NULL);

    if (gfiles) free(gfiles);
}

/* ============================================================
** ufsd_gft_alloc
**
** Find the first unused GFT slot, initialise its eye catcher,
** mark it UFSD_GF_USED, and return its index in *out_idx.
** Returns UFSD_RC_OK or UFSD_RC_NOREQ (no free slot).
** ============================================================ */
int
ufsd_gft_alloc(UFSD_ANCHOR *anchor, unsigned *out_idx)
{
    UFSD_GFILE *gfiles;
    unsigned    i;

    if (!anchor || !anchor->gfiles || !out_idx) return UFSD_RC_IO;

    gfiles = anchor->gfiles;

    for (i = 0; i < anchor->max_gfiles; i++) {
        if (!(gfiles[i].flags & UFSD_GF_USED)) {
            memset(&gfiles[i], 0, sizeof(UFSD_GFILE));
            memcpy(gfiles[i].eye, "UFSDGFIL", 8);
            gfiles[i].flags = UFSD_GF_USED;
            *out_idx = i;
            return UFSD_RC_OK;
        }
    }

    return UFSD_RC_NOREQ;
}

/* ============================================================
** ufsd_gft_release
**
** Zero out the GFT slot at index 'idx', marking it free.
** ============================================================ */
void
ufsd_gft_release(UFSD_ANCHOR *anchor, unsigned idx)
{
    if (!anchor || !anchor->gfiles) return;
    if (idx >= anchor->max_gfiles) return;

    memset(&anchor->gfiles[idx], 0, sizeof(UFSD_GFILE));
}
