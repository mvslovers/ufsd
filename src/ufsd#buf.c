/* UFSD#BUF.C - Buffer pool alloc / free
**
** CS-based lock-free push/pop on the UFSD_ANCHOR buffer free chain.
** Same pattern as the request pool in ufsd#ssi.c.
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

#include "ufsd.h"

/* ============================================================
** ufsd_buf_alloc
**
** CS-pop one 4K buffer from the CSA buffer pool.
** Returns NULL if the pool is exhausted (caller must fall back
** to the 252-byte inline path).
** Must be called from supervisor state (PSW key 0).
** ============================================================ */
UFSBUF *
ufsd_buf_alloc(UFSD_ANCHOR *anchor)
{
    UFSBUF   *buf;
    unsigned  old;
    unsigned  nxt;

    for (;;) {
        buf = anchor->buf_free;
        if (!buf) return NULL;
        old = (unsigned)buf;
        nxt = (unsigned)buf->next;
        __asm__ __volatile__(
            "CS %0,%2,%1"
            : "+r"(old), "+m"(anchor->buf_free)
            : "r"(nxt)
            : "cc");
        if (old == (unsigned)buf) {
            buf->next = NULL;
            __udec(&anchor->buf_count);
            return buf;
        }
    }
}

/* ============================================================
** ufsd_buf_free
**
** CS-push one 4K buffer back onto the CSA buffer pool.
** Must be called from supervisor state (PSW key 0).
** ============================================================ */
void
ufsd_buf_free(UFSD_ANCHOR *anchor, UFSBUF *buf)
{
    unsigned old;

    if (!buf) return;

    for (;;) {
        buf->next = anchor->buf_free;
        old = (unsigned)buf->next;
        __asm__ __volatile__(
            "CS %0,%2,%1"
            : "+r"(old), "+m"(anchor->buf_free)
            : "r"((unsigned)buf)
            : "cc");
        if (old == (unsigned)buf->next) {
            __uinc(&anchor->buf_count);
            return;
        }
    }
}
