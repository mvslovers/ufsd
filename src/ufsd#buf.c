/* UFSD#BUF.C - Buffer pool alloc / free
**
** Lock-free push/pop on the UFSD_ANCHOR buffer free chain: CDS on the
** pool head and its tag (ufsd#pol.c), the same as the request pool.
**
** Split out from UFSD#CSA so that UFSDSSIR (the thin SSI router)
** can link ufsd_buf_free without pulling in the full CSA module.
**
** IMPORTANT: Both functions must be called from supervisor state
** (PSW key 0).  They do NOT switch state internally, because both
** call sites (ufsd_dispatch and ufsdssir) are already inside their
** own __super/__prob blocks.  Nesting __super/__prob would drop
** the caller to problem state on return, causing 0C2/0C4 abends
** in the subsequent supervisor-state code.
*/

#include <stddef.h>
#include <s370/atomic.h>
#include "ufsd.h"

/* ============================================================
** ufsd_buf_alloc
**
** Pop one 4K buffer from the CSA buffer pool.
** Returns NULL if the pool is exhausted (caller must fall back
** to the 252-byte inline path).
** Must be called from supervisor state (PSW key 0).
** ============================================================ */
UFSBUF *
ufsd_buf_alloc(UFSD_ANCHOR *anchor)
{
    UFSBUF *buf;

    buf = (UFSBUF *)ufsd_pool_pop(&anchor->buf_pool,
                                  (unsigned)offsetof(UFSBUF, next));
    if (buf) __udec(&anchor->buf_count);
    return buf;
}

/* ============================================================
** ufsd_buf_free
**
** Push one 4K buffer back onto the CSA buffer pool.
** Must be called from supervisor state (PSW key 0).
** ============================================================ */
void
ufsd_buf_free(UFSD_ANCHOR *anchor, UFSBUF *buf)
{
    if (!buf) return;

    ufsd_pool_push(&anchor->buf_pool, buf, (unsigned)offsetof(UFSBUF, next));
    __uinc(&anchor->buf_count);
}
