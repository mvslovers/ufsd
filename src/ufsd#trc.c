/* UFSD#TRC.C - Trace Ring Buffer
**
** AP-1b: Write one entry to the trace ring buffer in CSA.
**
** The ring is located in CSA (key 0); writes require supervisor
** state.  The TOD clock (STCK) also requires supervisor state
** on S/370.
**
** Ring wrap-around is silent: the oldest entry is overwritten
** when the ring is full.
*/

#include "ufsd.h"
#include <clibos.h>

/* ============================================================
** ufsd_trace
**
** Record one trace event.  Safe to call with anchor == NULL
** or trace_buf == NULL (no-op).
**
**   anchor - server anchor block
**   func   - event code (UFSD_T_*)
**   token  - session token (or 0)
**   rc     - return code (or 0)
** ============================================================ */
void
ufsd_trace(UFSD_ANCHOR *anchor, unsigned short func,
           unsigned token, unsigned short rc)
{
    unsigned char   savekey;
    UFSD_TRACE     *entry;
    /* STCK needs doubleword alignment; allocate 16 bytes and align */
    unsigned        clk[4];
    unsigned       *clkp;
    unsigned        idx;

    if (!anchor || !anchor->trace_buf) return;

    if (__super(PSWKEY0, &savekey)) return;

    /* Get TOD clock.  STCK requires supervisor state and 8-byte
    ** alignment.  We align clk[] manually to guarantee it. */
    clkp = (unsigned *)(((unsigned)clk + 7U) & ~7U);
    __asm__("STCK 0(%0)" : : "r"(clkp) : "memory");

    idx   = anchor->trace_next;
    entry = &anchor->trace_buf[idx];

    entry->timestamp     = clkp[1]; /* low 32 bits of TOD clock */
    entry->func          = func;
    entry->rc            = rc;
    entry->session_token = token;
    entry->asid          = 0;       /* AP-1d: fill from current ASCB */

    anchor->trace_next = (idx + 1 < anchor->trace_size) ? idx + 1 : 0;

    __prob(savekey, NULL);
}
