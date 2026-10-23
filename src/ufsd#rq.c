/* UFSD#RQ.C - the request queue: client push, server pop (#159)
**
** Many clients, one server, no lock.  Clients push onto req_head, a LIFO
** chain, with one CS.  The server takes the whole chain with one CS that
** swaps req_head for NULL, reverses it into FIFO order and keeps it in
** req_ready, which only the server touches.  The server never changes a
** word a client can change except by that one swap, so there is no window
** in which an append and a dequeue can disagree about where the queue
** ends (the old head/tail pair had one: a request appended while the
** server took the last one was linked behind a block already gone).
**
** A push cannot go wrong through ABA: it only stores what it read into the
** new request's chain word, and the CS fails unless the head is what it
** read.  The take stores NULL, not a successor it read earlier, so it gets
** exactly the chain that was there when the CS succeeded.
**
** Portable on purpose: the only machine-dependent step is the CS in
** UFSD#RQC, so test/host/tstufsrq.c can drive these two functions with a CS
** stand-in that lets another "CPU" run between the read and the swap.
**
** Callers are in key 0 (the queue words and the requests are CSA); no
** state switch here.  No writable static: UFSD and UFSDSSIR are AC(1)
** (CLAUDE.md, 7).
*/

#include <stddef.h>
#include "ufsd.h"

/* Push req.  Returns 1 if the chain was empty, 0 otherwise.  Only a push
** that found it empty posts the server (UFSDSSIR): the server looks at
** req_head again after it resets its ECB, so one that found it non-empty
** is picked up by that look or by the take already under way. */
int
ufsd_rq_push(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    UFSREQ *seen;

    seen = *(UFSREQ * volatile *)&anchor->req_head;
    for (;;) {
        req->next = seen;
        if (ufsd_rq_cs(&anchor->req_head, &seen, req) == 0)
            return seen == NULL;
        /* seen is the head now; link behind that and try again */
    }
}

/* The next request in the order they were pushed, NULL if there is none. */
UFSREQ *
ufsd_rq_pop(UFSD_ANCHOR *anchor)
{
    UFSREQ *chain;
    UFSREQ *next;
    UFSREQ *fifo;
    UFSREQ *req;

    req = anchor->req_ready;
    if (req == NULL) {
        chain = *(UFSREQ * volatile *)&anchor->req_head;
        for (;;) {
            if (chain == NULL) return NULL;
            if (ufsd_rq_cs(&anchor->req_head, &chain, NULL) == 0) break;
            /* chain is the head now; take that one */
        }
        /* The chain is ours alone now: newest first, turn it around. */
        fifo = NULL;
        while (chain != NULL) {
            next        = chain->next;
            chain->next = fifo;
            fifo        = chain;
            chain       = next;
        }
        req = fifo;
    }
    if (req == NULL) return NULL;
    anchor->req_ready = req->next;
    req->next         = NULL;
    return req;
}
