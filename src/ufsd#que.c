/* UFSD#QUE.C - Server-Side Request Queue and Dispatch
**
** AP-1c: Dequeue, validate, and dispatch incoming UFSREQ blocks.
**        Called from the STC main loop (ufsd.c).
**
** All operations that touch CSA (key 0) use __super/__prob.
**
** IMPORTANT: ecb_post (SVC 2) causes S102 for cross-address-space
** POST.  All client ECB notifications use __xmpost (CVT0PT01 branch
** entry) from supervisor state.  __xmpost does not issue SVC 2.
**
** IMPORTANT: ufsd_req_free must NOT be called from dispatch.  The
** req block is owned by the SSI router (client side) until after
** the client's WAIT returns.  The SSI router frees the block via
** CS-push after copying the result.  Double-freeing causes pool
** corruption.
**
** ufsd_server_ecb_reset  - zero server_ecb before WAIT
** ufsd_dequeue           - pop one request from req_head
** ufsd_dispatch          - validate + dispatch + post client ECB
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>
#include <clibos.h>
#include <clibecb.h>
#include <clibwto.h>

/* ============================================================
** ufsd_server_ecb_reset
**
** Clear anchor->server_ecb to 0 so the next WAIT blocks.
** Must be called (with anchor still valid) before each WAIT.
** ============================================================ */
void
ufsd_server_ecb_reset(UFSD_ANCHOR *anchor)
{
    unsigned char savekey;

    if (!anchor) return;
    if (__super(PSWKEY0, &savekey)) return;
    anchor->server_ecb = 0;
    __prob(savekey, NULL);
}

/* ============================================================
** ufsd_dequeue
**
** Pop one UFSREQ from the head of the active request queue.
** Returns NULL if the queue is empty.
**
** The server is the sole consumer; no CS lock is needed here.
** Writes to req_head/req_tail still require key 0 (CSA).
** ============================================================ */
UFSREQ *
ufsd_dequeue(UFSD_ANCHOR *anchor)
{
    unsigned char   savekey;
    UFSREQ         *req;

    if (!anchor) return NULL;
    if (!anchor->req_head) return NULL;  /* quick check, no key needed */

    if (__super(PSWKEY0, &savekey)) return NULL;

    req = anchor->req_head;
    if (req) {
        anchor->req_head = req->next;
        if (!anchor->req_head) anchor->req_tail = NULL;
        req->next = NULL;
    }

    __prob(savekey, NULL);
    return req;
}

/* ============================================================
** ufsd_dispatch
**
** Validate a dequeued UFSREQ, execute the function, write the
** result into the request block (key 0), then post the client
** ECB via __xmpost (CVT0PT01) from supervisor state.
**
** The req block is NOT freed here.  The SSI router (client side)
** frees it after copying the result from req back to UFSSSOB.
**
** Trace events are recorded around the dispatch window.
** ============================================================ */
void
ufsd_dispatch(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    unsigned char   savekey;
    int             rc;
    unsigned        out_token;  /* SESS_OPEN response token */
    char            resp_data[UFSREQ_MAX_INLINE];
    unsigned        resp_data_len;
    UFSD_SESSION   *sess;
    char           *stg_free;   /* staging buffer to free after key-0 exit */

    if (!anchor || !req) return;
    out_token     = 0;
    resp_data_len = 0;
    sess          = NULL;
    stg_free      = NULL;

    /* --- Eye-catcher check --- */
    if (memcmp(req->eye, "UFSREQ__", 8) != 0) {
        wtof("UFSD-DBG eye mismatch");
        ufsd_trace(anchor, UFSD_T_CORRUPT, 0, (unsigned short)UFSD_RC_CORRUPT);
        /* Write error stat and post client ECB (key 0 for CSA write) */
        if (!__super(PSWKEY0, &savekey)) {
            anchor->stat_errors++;
            __xmpost(req->client_ascb, req->client_ecb_ptr, 0);
            __prob(savekey, NULL);
        }
        /* Do NOT return corrupt block to pool: unknown state */
        return;
    }

    /* --- Function code check --- */
    if (req->func < UFSREQ_MIN || req->func > UFSREQ_MAX) {
        wtof("UFSD-DBG bad func %04X", req->func);
        ufsd_trace(anchor, UFSD_T_BADFUNC, req->session_token,
                   (unsigned short)UFSD_RC_BADFUNC);
        if (!__super(PSWKEY0, &savekey)) {
            req->rc = UFSD_RC_BADFUNC;
            req->errno_val = 0;
            anchor->stat_errors++;
            __xmpost(req->client_ascb, req->client_ecb_ptr, 0);
            __prob(savekey, NULL);
        }
        return;
    }

    ufsd_trace(anchor, UFSD_T_REQUEST, req->session_token,
               (unsigned short)req->func);

    /* --- Dispatch by function code --- */
    switch (req->func) {

    case UFSREQ_PING:
        rc = UFSD_RC_OK;
        break;

    case UFSREQ_SESS_OPEN:
        rc = ufsd_sess_open(anchor, req, &out_token);
        if (rc == UFSD_RC_OK)
            ufsd_trace(anchor, UFSD_T_SESS_OPEN, out_token, 0);
        break;

    case UFSREQ_SESS_CLOSE:
        rc = ufsd_sess_close(anchor, req);
        if (rc == UFSD_RC_OK)
            ufsd_trace(anchor, UFSD_T_SESS_CLOSE, req->session_token, 0);
        break;

    case UFSREQ_SETUSER:
        sess = ufsd_sess_find(anchor, req->session_token);
        if (!sess) {
            rc = UFSD_RC_BADSESS;
            ufsd_trace(anchor, UFSD_T_BADSESS, req->session_token,
                       (unsigned short)UFSD_RC_BADSESS);
        } else {
            rc = ufsd_sess_setuser(anchor, req);
        }
        break;

    case UFSREQ_FOPEN:
    case UFSREQ_FCLOSE:
    case UFSREQ_FREAD:
    case UFSREQ_FWRITE:
    case UFSREQ_MKDIR:
    case UFSREQ_RMDIR:
    case UFSREQ_CHGDIR:
    case UFSREQ_REMOVE:
    case UFSREQ_GETCWD:   /* AP-1f */
    case UFSREQ_DIROPEN:  /* AP-1f */
    case UFSREQ_DIRREAD:  /* AP-1f */
    case UFSREQ_DIRCLOSE: /* AP-1f */
        sess = ufsd_sess_find(anchor, req->session_token);
        if (!sess) {
            rc = UFSD_RC_BADSESS;
            ufsd_trace(anchor, UFSD_T_BADSESS, req->session_token,
                       (unsigned short)UFSD_RC_BADSESS);
            break;
        }
        rc = ufsd_fil_dispatch(anchor, sess, req, resp_data, &resp_data_len);
        break;

    default:
        rc = UFSD_RC_NOTIMPL;
        break;
    }

    /* --- Write result + post client ECB (key 0 for CSA write + __xmpost) ---
    ** __xmpost uses CVT0PT01 (POST branch entry), not SVC 2.
    ** It works for cross-AS POST and must be called from supervisor state.
    ** req->client_ecb_ptr -> CSA ECB (key 0), accessible via CVT0PT01.
    ** req->client_ascb is the waiting task's ASCB (saved by ufsdssir).
    */
    if (!__super(PSWKEY0, &savekey)) {
        req->rc        = rc;
        req->errno_val = 0;
        req->data_len  = 0;

        /* SESS_OPEN: return the new token in data[0..3] */
        if (req->func == UFSREQ_SESS_OPEN && rc == UFSD_RC_OK) {
            req->data_len          = (unsigned)sizeof(unsigned);
            *(unsigned *)req->data = out_token;
        } else if (req->func == UFSREQ_FREAD && rc == UFSD_RC_OK
                   && resp_data_len == 4U) {
            /* 4K FREAD path: staging buffer pointer in resp_data[4..7].
            ** Allocate CSA pool buffer, copy from heap staging, free it. */
            unsigned bread;
            char    *stg;

            bread = *(unsigned *)resp_data;
            stg   = *(char **)(resp_data + 4);
            req->data_len = resp_data_len;
            memcpy(req->data, resp_data, 4U);  /* bytes_read count */
            if (stg && bread > 0) {
                req->buf = ufsd_buf_alloc(anchor);
                if (req->buf)
                    memcpy(req->buf->data, stg, bread);
                stg_free = stg;  /* defer free() until after __prob */
            }
        } else if (resp_data_len > 0) {
            /* AP-1e: copy file op response from STC stack to CSA block */
            req->data_len = resp_data_len;
            memcpy(req->data, resp_data, resp_data_len);
        }

        /* FWRITE 4K path: free the CSA buffer after dispatch consumed it */
        if (req->func == UFSREQ_FWRITE && req->buf != NULL) {
            ufsd_buf_free(anchor, req->buf);
            req->buf = NULL;
        }

        if (rc == UFSD_RC_OK) {
            anchor->stat_requests++;
        } else if (rc == UFSD_RC_IO      ||
                   rc == UFSD_RC_NOSPACE ||
                   rc == UFSD_RC_NOINODES||
                   rc == UFSD_RC_CORRUPT ||
                   rc == UFSD_RC_BADFUNC ||
                   rc == UFSD_RC_BADSESS ||
                   rc == UFSD_RC_INVALID) {
            /* Hard errors only: I/O failures and protocol violations.
            ** Soft filesystem responses (NOFILE, ISDIR, EXIST, etc.)
            ** are normal outcomes and do not increment the error counter. */
            anchor->stat_errors++;
        } else {
            anchor->stat_requests++;
        }

        __xmpost(req->client_ascb, req->client_ecb_ptr, 0);
        __prob(savekey, NULL);
    }

    /* Free staging buffer in problem state (key 8).
    ** free() issues FREEMAIN SVC which requires the caller's PSW key
    ** to match the storage key.  The staging buffer was malloc'd in
    ** do_fread (problem state, key 8), so it must be freed here,
    ** NOT inside the key-0 block above (S378 abend). */
    if (stg_free) free(stg_free);

    ufsd_trace(anchor, UFSD_T_COMPLETE, req->session_token,
               (unsigned short)rc);

    /* req block is freed by the SSI router after WAIT returns */
}
