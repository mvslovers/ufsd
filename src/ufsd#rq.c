/* UFSD#RQ.C - the request queue: client push, server pop (#159)
**
** Portable on purpose: the machine-dependent steps are in UFSD#RQC, so
** test/host/tstufsrq.c can drive these two functions and let a second
** "CPU" run at the points where the real CPUs can.
**
** Callers are in key 0 (the queue words and the requests are CSA); no
** state switch here.  No writable static: UFSD and UFSDSSIR are AC(1)
** (CLAUDE.md, 7).
*/

#include <stddef.h>
#include "ufsd.h"

/* Append req behind the queue.  Returns 1 if the queue was empty. */
int
ufsd_rq_push(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    int was_empty;

    req->next = NULL;
    ufsd_rq_lock(anchor);
    was_empty = (anchor->req_head == NULL);
    if (anchor->req_tail) {
        anchor->req_tail->next = req;
    } else {
        anchor->req_head = req;
    }
    anchor->req_tail = req;
    ufsd_rq_unlock(anchor);
    return was_empty;
}

/* Take the request at the head of the queue, NULL if there is none. */
UFSREQ *
ufsd_rq_pop(UFSD_ANCHOR *anchor)
{
    UFSREQ *req;

    req = anchor->req_head;
    if (req) {
        anchor->req_head = req->next;
        ufsd_rq_window();
        if (!anchor->req_head) anchor->req_tail = NULL;
        req->next = NULL;
    }
    return req;
}
