/* UFSD#BUF.C - Buffer pool alloc / free
**
** Trivial push/pop on the UFSD_ANCHOR buffer free chain.
** Split out from UFSD#CSA so that UFSDSSIR (the thin SSI router)
** can link ufsd_buf_free without pulling in the full CSA module.
**
** Both functions enter supervisor state (PSW key 0) internally
** so callers in problem state can use them safely.
*/

#include "ufsd.h"
#include <clibos.h>

/* ============================================================
** ufsd_buf_alloc
**
** Pop one 4K buffer from the CSA buffer pool.
** Returns NULL if the pool is exhausted (caller must fall back
** to the 252-byte inline path).
** Enters supervisor state internally for CSA writes.
** ============================================================ */
UFSBUF *
ufsd_buf_alloc(UFSD_ANCHOR *anchor)
{
    UFSBUF         *buf;
    unsigned char   savekey;

    if (__super(PSWKEY0, &savekey)) return NULL;

    buf = anchor->buf_free;
    if (buf) {
        anchor->buf_free = buf->next;
        anchor->buf_count--;
        buf->next = NULL;
    }

    __prob(savekey, NULL);
    return buf;
}

/* ============================================================
** ufsd_buf_free
**
** Push one 4K buffer back onto the CSA buffer pool.
** Enters supervisor state internally for CSA writes.
** ============================================================ */
void
ufsd_buf_free(UFSD_ANCHOR *anchor, UFSBUF *buf)
{
    unsigned char savekey;

    if (!buf) return;
    if (__super(PSWKEY0, &savekey)) return;

    buf->next        = anchor->buf_free;
    anchor->buf_free = buf;
    anchor->buf_count++;

    __prob(savekey, NULL);
}
