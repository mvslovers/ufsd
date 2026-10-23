/* UFSD#CSA.C - CSA Anchor and Pool Management
**
** AP-1b: Anchor allocation (CSA SP=241 key 0), request pool,
** buffer pool, and trace ring buffer.  All CSA writes require
** supervisor state with PSW key 0.  Caller must be APF authorized.
*/

#include "ufsd.h"
#include <stddef.h>
#include <string.h>
#include <mvs/apf.h>
#include <s370/atomic.h>
#include <mvs/storage.h>
#include <mvs/wto.h>

/* Anchor layout 2 (#142): the CDS heads must be doubleword aligned (S0C6
** otherwise) and the offsets a predecessor's reclaim reads must not move.
** A violation makes the array size negative, i.e. fails the build. */
typedef char ufsd_layout_req_pool_aligned[(offsetof(UFSD_ANCHOR, req_pool) % 8 == 0) ? 1 : -1];
typedef char ufsd_layout_buf_pool_aligned[(offsetof(UFSD_ANCHOR, buf_pool) % 8 == 0) ? 1 : -1];
typedef char ufsd_layout_v1_free_count[(offsetof(UFSD_ANCHOR, free_count) == 32) ? 1 : -1];
typedef char ufsd_layout_v1_buf_count[(offsetof(UFSD_ANCHOR, buf_count) == 44) ? 1 : -1];
typedef char ufsd_layout_v1_inflight[(offsetof(UFSD_ANCHOR, inflight) == 128) ? 1 : -1];
typedef char ufsd_layout_pool_after_v1[(offsetof(UFSD_ANCHOR, req_pool) >= 132) ? 1 : -1];

/* ============================================================
** ufsd_anchor_alloc
**
** Allocate UFSD_ANCHOR in CSA (SP=241, key 0).
** Initialises the eye catcher, version, and flags.
** Returns pointer, or NULL on failure.
** ============================================================ */
UFSD_ANCHOR *
ufsd_anchor_alloc(void)
{
    UFSD_ANCHOR    *anchor;
    unsigned char   savekey;

    if (__super(PSWKEY0, &savekey)) return NULL;
    anchor = (UFSD_ANCHOR *)getmain((unsigned)sizeof(UFSD_ANCHOR), 241);
    if (anchor) {
        memset(anchor, 0, sizeof(UFSD_ANCHOR));
        memcpy(anchor->eye, "UFSDANCR", 8);
        anchor->version = UFSD_ANCHOR_VERSION;
        anchor->flags   = UFSD_ANCHOR_ACTIVE | UFSD_ANCHOR_TRACE_ON;
    }
    __prob(savekey, NULL);
    return anchor;
}

/* ============================================================
** ufsd_anchor_free
**
** Release UFSD_ANCHOR back to CSA.
** ============================================================ */
void
ufsd_anchor_free(UFSD_ANCHOR *anchor)
{
    unsigned char savekey;

    if (!anchor) return;
    if (__super(PSWKEY0, &savekey)) return;
    /* Invalidate the eye catcher BEFORE releasing the block.  Freed
    ** SP=241 storage is reused, not zeroed, so a client still looping
    ** in ufsdssir must not accept the reused anchor as valid: its
    ** timeout-branch revalidates "UFSDANCR" and bails once it is gone. */
    memset(anchor->eye, 0, sizeof(anchor->eye));
    freemain(anchor);
    __prob(savekey, NULL);
}

/* ============================================================
** ufsd_csa_init
**
** Allocate and initialise all CSA pools in a single supervisor-
** state window:
**
**   UFSD_REQ_POOL_COUNT x UFSREQ  (request block pool)
**   UFSD_BUF_POOL_COUNT x UFSBUF  (4K buffer pool)
**   UFSD_TRACE_SIZE     x UFSD_TRACE (trace ring buffer)
**
** Each pool is one contiguous GETMAIN.  Free chains are built
** over the contiguous array while still in key 0.  Individual
** items must never be passed to freemain(); only req_pool_base,
** buf_pool_base, and trace_buf are freed at shutdown.
**
** Returns 0 on success, -1 on failure (WTO issued on error).
** ============================================================ */
int
ufsd_csa_init(UFSD_ANCHOR *anchor)
{
    unsigned char   savekey;
    UFSREQ         *reqs;
    UFSBUF         *bufs;
    UFSD_TRACE     *trace;
    unsigned        i;

    if (__super(PSWKEY0, &savekey)) {
        wtof("UFSD038E CANNOT ENTER SUPERVISOR STATE FOR CSA INIT");
        return -1;
    }

    /* --- Request pool --- */
    reqs = (UFSREQ *)getmain(
        UFSD_REQ_POOL_COUNT * (unsigned)sizeof(UFSREQ), 241);
    if (!reqs) goto fail;

    /* --- Buffer pool --- */
    bufs = (UFSBUF *)getmain(
        UFSD_BUF_POOL_COUNT * (unsigned)sizeof(UFSBUF), 241);
    if (!bufs) { freemain(reqs); goto fail; }

    /* --- Trace ring buffer --- */
    trace = (UFSD_TRACE *)getmain(
        UFSD_TRACE_SIZE * (unsigned)sizeof(UFSD_TRACE), 241);
    if (!trace) { freemain(bufs); freemain(reqs); goto fail; }

    /* Build request free chain */
    memset(reqs, 0, UFSD_REQ_POOL_COUNT * sizeof(UFSREQ));
    for (i = 0; i < UFSD_REQ_POOL_COUNT; i++) {
        memcpy(reqs[i].eye, "UFSREQ__", 8);
        reqs[i].next = (i + 1 < UFSD_REQ_POOL_COUNT)
                       ? &reqs[i + 1] : NULL;
    }

    /* Build buffer free chain */
    memset(bufs, 0, UFSD_BUF_POOL_COUNT * sizeof(UFSBUF));
    for (i = 0; i < UFSD_BUF_POOL_COUNT; i++) {
        bufs[i].next = (i + 1 < UFSD_BUF_POOL_COUNT)
                       ? &bufs[i + 1] : NULL;
    }

    /* Clear trace ring */
    memset(trace, 0, UFSD_TRACE_SIZE * sizeof(UFSD_TRACE));

    /* Populate anchor fields (anchor is in CSA, requires key 0) */
    anchor->req_pool_base = reqs;
    anchor->req_pool.head = reqs;
    anchor->req_pool.tag  = 0;
    anchor->free_count    = UFSD_REQ_POOL_COUNT;
    anchor->total_reqs    = UFSD_REQ_POOL_COUNT;

    anchor->buf_pool_base = bufs;
    anchor->buf_pool.head = bufs;
    anchor->buf_pool.tag  = 0;
    anchor->buf_count     = UFSD_BUF_POOL_COUNT;
    anchor->buf_total     = UFSD_BUF_POOL_COUNT;

    anchor->trace_buf  = trace;
    anchor->trace_next = 0;
    anchor->trace_size = UFSD_TRACE_SIZE;

    __prob(savekey, NULL);
    return 0;

fail:
    __prob(savekey, NULL);
    wtof("UFSD039E CANNOT ALLOCATE CSA POOLS");
    return -1;
}

/* ============================================================
** ufsd_csa_free
**
** Release all CSA pools back to the system.
** Fields are nulled inside key 0 so concurrent readers see
** consistent state.
** ============================================================ */
void
ufsd_csa_free(UFSD_ANCHOR *anchor)
{
    unsigned char savekey;

    if (!anchor) return;
    if (__super(PSWKEY0, &savekey)) return;

    if (anchor->trace_buf) {
        freemain(anchor->trace_buf);
        anchor->trace_buf = NULL;
    }
    if (anchor->buf_pool_base) {
        freemain(anchor->buf_pool_base);
        anchor->buf_pool_base = NULL;
        anchor->buf_pool.head = NULL;
    }
    if (anchor->req_pool_base) {
        freemain(anchor->req_pool_base);
        anchor->req_pool_base = NULL;
        anchor->req_pool.head = NULL;
    }

    __prob(savekey, NULL);
}

/* ============================================================
** ufsd_req_alloc
**
** Pop one request block from the free pool.
** Returns NULL if the pool is exhausted.
** ============================================================ */
UFSREQ *
ufsd_req_alloc(UFSD_ANCHOR *anchor)
{
    UFSREQ         *req;
    unsigned char   savekey;

    if (__super(PSWKEY0, &savekey)) return NULL;

    req = (UFSREQ *)ufsd_pool_pop(&anchor->req_pool,
                                  (unsigned)offsetof(UFSREQ, next));
    if (req) __udec(&anchor->free_count);

    __prob(savekey, NULL);
    return req;
}

/* ============================================================
** ufsd_req_free
**
** Push one request block back onto the free pool.
** ============================================================ */
void
ufsd_req_free(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    unsigned char savekey;

    if (!req) return;
    if (__super(PSWKEY0, &savekey)) return;

    ufsd_pool_push(&anchor->req_pool, req, (unsigned)offsetof(UFSREQ, next));
    __uinc(&anchor->free_count);

    __prob(savekey, NULL);
}

/* ufsd_buf_alloc / ufsd_buf_free  -->  moved to ufsd#buf.c */
