/* UFSD#BUF.C - Buffer pool alloc / free
**
** Trivial push/pop on the UFSD_ANCHOR buffer free chain.
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

    buf = anchor->buf_free;
    if (buf) {
        anchor->buf_free = buf->next;
        anchor->buf_count--;
        buf->next = NULL;
    }
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

    buf->next        = anchor->buf_free;
    anchor->buf_free = buf;
    anchor->buf_count++;
}
