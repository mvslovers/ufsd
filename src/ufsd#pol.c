/* UFSD#POL.C - lock-free pop / push on a CDS-protected pool head (#142)
**
** The request pool and the 4K buffer pool are LIFO free chains in CSA.
** Their head is a UFSPOOL: first free item plus a tag, changed only as a
** pair by ufsd_pool_cds().  The tag is bumped on every pop and push, so a
** snapshot taken before another CPU popped an item and pushed it back
** (ABA) no longer matches: the compare fails and the pop starts over.
** With the head word alone, such a pop would have installed the stale
** successor and left one block on the chain and in use at the same time.
**
** Portable on purpose: the only machine-dependent step is the CDS in
** UFSD#CDS, so test/host/tstufspl.c can drive these two functions with a
** CDS stand-in that lets another "CPU" run between snapshot and swap.
**
** No writable static: UFSD and UFSDSSIR are AC(1) (CLAUDE.md, 7).
**
** Stale reads are harmless.  A pop reads the successor of the item it saw
** at the head; by then that item may belong to someone else and hold
** anything.  The value is only ever handed to the CDS as the new head, and
** the CDS fails unless the tag proves nothing changed -- it is never
** dereferenced.  The items are slots of the CSA pools, which stay mapped
** until shutdown has drained the in-flight clients, so the read itself
** cannot fault.
**
** Callers are in key 0 (the items are CSA); no state switch here.
*/

#include <stddef.h>
#include "ufsd.h"

#define POOL_LINK(item, off)  (*(void **)((char *)(item) + (off)))

/* The two words are not read atomically.  A torn snapshot (head of one
** state, tag of the next) is no state the pool was ever in, so the CDS
** fails on it and returns the real contents. */
static void
pool_snapshot(UFSPOOL *pool, UFSPOOL *seen)
{
    seen->head = ((volatile UFSPOOL *)pool)->head;
    seen->tag  = ((volatile UFSPOOL *)pool)->tag;
}

void *
ufsd_pool_pop(UFSPOOL *pool, unsigned link_off)
{
    UFSPOOL seen;
    void   *item;

    pool_snapshot(pool, &seen);
    for (;;) {
        item = seen.head;
        if (item == NULL) return NULL;
        if (ufsd_pool_cds(pool, &seen, POOL_LINK(item, link_off),
                          seen.tag + 1U) == 0) {
            POOL_LINK(item, link_off) = NULL;
            return item;
        }
        /* seen now holds the current head and tag; try again */
    }
}

void
ufsd_pool_push(UFSPOOL *pool, void *item, unsigned link_off)
{
    UFSPOOL seen;

    pool_snapshot(pool, &seen);
    for (;;) {
        POOL_LINK(item, link_off) = seen.head;
        if (ufsd_pool_cds(pool, &seen, item, seen.tag + 1U) == 0)
            return;
    }
}
