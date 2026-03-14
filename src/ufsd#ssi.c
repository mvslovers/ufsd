/* UFSD#SSI.C - SSI Thin Router
**
** AP-1c: Entry point UFSDSSIR, called by IEFSSREQ (SVC 34).
**
** This module is loaded into CSA (SP=241) via __loadhi() and
** runs in the CLIENT's address space, NOT in the STC.  It must
** be self-contained: no references to STC-local storage.
**
** Calling context (set by MVS before invoking the SSVT routine):
**   - Supervisor state, PSW key 0
**   - R1 = address of SSOB
**   - Running in CLIENT address space
**
** Calling convention mismatch:
**   MVS passes R1 = SSOB address (raw pointer, not a C plist).
**   c2asm370 expects R1 = pointer to parameter list (C convention).
**   ufsdssir() is declared void() and extracts ssob from R1 via
**   inline asm before the compiler generates any plist dereference.
**
** Flow:
**   1. Validate SSOB + UFSSSOB extension
**   2. Find UFSD_ANCHOR via ssct_find("UFSD")->ssctsuse
**   3. CS-pop one UFSREQ from the free pool
**   4. Fill UFSREQ fields from UFSSSOB
**   5. CS-enqueue UFSREQ at req_tail (req_lock spin)
**   6. POST anchor->server_ecb to wake the STC
**   7. WAIT on req->client_ecb for the STC to reply
**   8. Copy result back to UFSSSOB
**   9. Return UFSREQ to free pool
**  10. Set SSOB.SSOBRETN, return to IEFSSREQ
**
** Because this module is RENT (no writeable statics), all state
** lives on the stack or in CSA structures.
*/

#include "ufsd.h"
#include <string.h>
#include <clibos.h>
#include <clibecb.h>
#include <clibssct.h>
#include <iefssobh.h>
#include <iefjssib.h>
#include <racf.h>

/* ============================================================
** Internal helpers (static, inline in the CSA load module)
** ============================================================ */

/*
** cs_lock_acquire: spin on anchor->req_lock using CS instruction.
**
** S/370 CS R1,R3,D2(B2):
**   compare memory[D2+B2] with R1;
**   if equal:  store R3 into memory, CC=0, R1 unchanged;
**   if unequal: load memory into R1,  CC=1, memory unchanged.
**
** We attempt to CS 0 (unlock) -> 1 (locked).
** If "old" comes back as 0, the CS succeeded; otherwise retry.
*/
static void
cs_lock_acquire(UFSD_ANCHOR *anchor)
{
    unsigned old;

    do {
        old = 0;
        __asm__ __volatile__(
            "CS %0,%2,%1"
            : "+r"(old), "+m"(anchor->req_lock)
            : "r"(1U)
            : "cc");
    } while (old != 0);
}

static void
cs_lock_release(UFSD_ANCHOR *anchor)
{
    anchor->req_lock = 0;
}

/*
** free_pop: CS-based lock-free pop from the free request pool.
**
** We load free_head and attempt to CS the pointer from *req to
** req->next.  If the CS succeeds (old == expected), the pop is
** done.  If another producer changed free_head between our load
** and our CS, the CS fails and we retry.
*/
static UFSREQ *
free_pop(UFSD_ANCHOR *anchor)
{
    UFSREQ  *req;
    unsigned old;
    unsigned nxt;

    for (;;) {
        req = anchor->free_head;
        if (!req) return NULL;
        old = (unsigned)req;
        nxt = (unsigned)req->next;
        __asm__ __volatile__(
            "CS %0,%2,%1"
            : "+r"(old), "+m"(anchor->free_head)
            : "r"(nxt)
            : "cc");
        if (old == (unsigned)req) break; /* CS succeeded */
        /* CS failed: old now contains current free_head; retry */
    }

    req->next = NULL;
    /* Decrement free_count (approximate; diagnostics only) */
    __udec(&anchor->free_count);
    return req;
}

/*
** queue_append: append req to the request queue tail.
**
** Uses req_lock (CS spin) to serialise multiple concurrent
** clients.  The server dequeues from req_head without a lock
** (single consumer).
*/
static void
queue_append(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    req->next = NULL;
    cs_lock_acquire(anchor);
    if (anchor->req_tail) {
        anchor->req_tail->next = req;
    } else {
        anchor->req_head = req;
    }
    anchor->req_tail = req;
    cs_lock_release(anchor);
}

/* ============================================================
** ufsdssir - SSI function routine entry point
**
** Called by MVS (via IEFSSREQ SVC 34) as the SSVT function
** routine for all UFSD subsystem requests.
**
** MVS SSVT calling convention: R1 = SSOB address (raw pointer).
** c2asm370 C calling convention: R1 = pointer to parameter list,
** first word = pointer to first arg.  These are INCOMPATIBLE.
**
** Fix: declare with no parameters.  c2asm370 then generates no
** plist-dereference code.  We extract the SSOB from R1 directly
** via inline asm before the compiler can touch it.
** ============================================================ */
void
ufsdssir(void) asm("UFSDSSIR");

void
ufsdssir(void)
{
    SSOB          *ssob;
    SSCT          *ssct;
    UFSD_ANCHOR   *anchor;
    UFSSSOB       *ufsssob;
    UFSREQ        *req;
    ECB            local_ecb;    /* key-8 stack ECB: WAIT target for this request */
    unsigned       dlen;
    unsigned char  savekey;

    /* iefssreq passes R1 = SSOB address; capture before any C code runs */
    __asm__ __volatile__("LR %0,1" : "=r"(ssob));

    /* --- Validate SSOB pointer and extension --- */
    if (!ssob) return;

    ufsssob = (UFSSSOB *)ssob->SSOBINDV;
    if (!ufsssob) {
        ssob->SSOBRETN = UFSD_RC_INVALID;
        return;
    }
    if (memcmp(ufsssob->eye, UFSSSOB_EYE, 4) != 0) {
        ssob->SSOBRETN = UFSD_RC_INVALID;
        return;
    }

    /* --- Locate CSA anchor via SSCT --- */
    ssct = ssct_find("UFSD");
    if (!ssct) {
        ssob->SSOBRETN = UFSD_RC_CORRUPT;
        return;
    }
    anchor = (UFSD_ANCHOR *)ssct->ssctsuse;
    if (!anchor || memcmp(anchor->eye, "UFSDANCR", 8) != 0) {
        ssob->SSOBRETN = UFSD_RC_CORRUPT;
        return;
    }
    if (!(anchor->flags & UFSD_ANCHOR_ACTIVE)) {
        ssob->SSOBRETN = UFSD_RC_CORRUPT;
        return;
    }

    /* --- Phase 1: key-0 window for CSA writes ---
    ** iefssreq issues MODESET MODE=SUP but NOT KEY=ZERO, so we arrive
    ** here with PSW key 8.  All UFSREQ pool and anchor fields are in
    ** CSA (SP=241, storage key 0).  CS/ST to key-0 from key-8 causes
    ** a protection exception (INTC=0x0004).
    ** The client task is APF-authorised (clib_apf_setup in ufsdping),
    ** so MODESET KEY=ZERO succeeds here.
    **
    ** IMPORTANT: __xmpost (CVT0PT01) must be called from supervisor state
    ** to wake the STC (cross-AS POST via SVC 2 causes S102).
    ** WAIT (SVC 1) must be issued from problem state on a key-8 ECB.
    ** WAIT from problem state on a key-0 CSA ECB causes X'201'.
    */
    if (__super(PSWKEY0, &savekey)) {
        ssob->SSOBRETN = UFSD_RC_CORRUPT;
        return;
    }

    /* --- Pop one request block from the free pool --- */
    req = free_pop(anchor);
    if (!req) {
        ssob->SSOBRETN = UFSD_RC_NOREQ;
        __prob(savekey, NULL);
        return;
    }

    /* --- Fill in the request block --- */
    local_ecb           = 0;             /* clear before storing pointer          */
    req->client_ecb_ptr = &local_ecb;    /* key-8 stack ECB, WAIT target          */
    req->client_ascb    = __ascb(0);     /* client ASCB: __xmpost uses this       */
    req->func           = ufsssob->func;
    req->session_token  = ufsssob->token;
    req->client_asid    = 0;     /* AP-1d: fill from current ASCB */
    req->rc             = 0;
    req->errno_val      = 0;
    req->buf            = NULL;

    dlen = ufsssob->data_len;
    if (dlen > UFSREQ_MAX_INLINE) dlen = UFSREQ_MAX_INLINE;
    req->data_len = dlen;
    if (dlen > 0) memcpy(req->data, ufsssob->data, dlen);

    /* SESS_OPEN: capture client userid/group from ACEE.
    ** We are in the client's address space in key-0, so
    ** racf_get_acee() returns the client's ACEE.
    ** aceeuser/aceegrp are length-prefixed: byte 0 = len,
    ** bytes 1..8 = name.  We strip the length byte and
    ** store a plain NUL-terminated string. */
    if (ufsssob->func == UFSREQ_SESS_OPEN) {
        ACEE *acee = racf_get_acee();
        if (acee) {
            unsigned char ulen = (unsigned char)acee->aceeuser[0];
            unsigned char glen = (unsigned char)acee->aceegrp[0];
            if (ulen > 8) ulen = 8;
            if (glen > 8) glen = 8;
            memset(req->data, 0, 18);
            memcpy(req->data,     acee->aceeuser + 1, ulen);
            memcpy(req->data + 9, acee->aceegrp  + 1, glen);
            req->data_len = 18;
        }
    }

    /* Save ECB pointer in a local (stack) variable before leaving key 0,
    ** so WAIT below can use it without reading from CSA in problem state. */
    {
        ECB *ecbp = req->client_ecb_ptr;

        /* --- Enqueue the request --- */
        queue_append(anchor, req);

        /* --- Wake the STC via cross-AS POST (from supervisor state) ---
        ** ecb_post (SVC 2) causes S102 for cross-address-space POST.
        ** __xmpost uses CVT0PT01 (POST branch entry) which does not
        ** issue SVC 2 and works correctly for cross-AS POST.
        ** anchor->server_ascb is the STC's ASCB, set at startup.
        ** We are still in key-0 supervisor state here.
        */
        __xmpost(anchor->server_ascb, &anchor->server_ecb, 0);

        /* --- Back to problem state before WAIT (SVC 1) ---
        ** WAIT SVC 1 must be issued from problem state on MVS 3.8j.
        */
        __prob(savekey, NULL);

        /* --- Wait for STC reply (WAIT SVC 1 from problem state) --- */
        __asm__ __volatile__("WAIT ECB=(%0)" : : "r"(ecbp));
    }

    /* --- Phase 2: key-0 window for reading CSA results + freeing req ---
    ** CSA storage has no fetch-protection, so req->* fields could be read
    ** from problem state.  We use a key-0 window anyway for consistency
    ** and to allow future fetch-protected pools.
    */
    if (!__super(PSWKEY0, &savekey)) {

        /*
        ** 4K buffer path: if the server filled a CSA buffer (req->buf != NULL),
        ** copy from it directly into the client's destination buffer (buf_ptr),
        ** then return the buffer to the pool.  bytes_read is in req->data[0..3].
        ** The router is in key 0 and in the client's address space, so writing
        ** to buf_ptr (key-8 client storage) is permitted.
        ** If buf_ptr is NULL the client did not provide a destination -- fall
        ** back silently (the bytes_read count is still returned in data[0..3]).
        */
        if (req->buf != NULL) {
            if (ufsssob->buf_ptr != NULL) {
                unsigned n = *(unsigned *)req->data; /* bytes_read */
                if (n > ufsssob->buf_len) n = ufsssob->buf_len;
                if (n > 0)
                    memcpy(ufsssob->buf_ptr, req->buf->data, n);
            }
            ufsd_buf_free(anchor, req->buf);
            req->buf = NULL;
        }

        /* Copy result back to SSOB extension */
        ufsssob->rc        = req->rc;
        ufsssob->errno_val = req->errno_val;

        dlen = req->data_len;
        if (dlen > UFSREQ_MAX_INLINE) dlen = UFSREQ_MAX_INLINE;
        ufsssob->data_len = dlen;
        if (dlen > 0) memcpy(ufsssob->data, req->data, dlen);

        ssob->SSOBRETN = req->rc;

        /* --- Return request block to the free pool (CS-push) --- */
        {
            UFSREQ  *old_head;
            unsigned old;
            for (;;) {
                old_head  = anchor->free_head;
                req->next = old_head;
                old       = (unsigned)old_head;
                __asm__ __volatile__(
                    "CS %0,%2,%1"
                    : "+r"(old), "+m"(anchor->free_head)
                    : "r"((unsigned)req)
                    : "cc");
                if (old == (unsigned)old_head) break;
            }
            __uinc(&anchor->free_count);
        }

        __prob(savekey, NULL);
    }
}
