/* UFSD#SES.C - Session Table Management
**
** AP-1d Step 1: Session infrastructure without UFS disk.
**   session->ufs is NULL in this step; filled in AP-1d Step 2.
**
** Token scheme: ((slot_index + 1) << 16) | (serial & 0xFFFF)
**   - Slot 0 produces tokens 0x0001xxxx
**   - serial is a static counter, wraps at 0xFFFF
**   - Token 0 is never issued (slot+1 ensures non-zero high word)
**
** Memory layout:
**   Session table: STC heap (calloc), NOT CSA.
**   anchor->sessions / anchor->max_sessions: CSA pointers,
**   written once at init inside a key-0 supervisor window.
**
** All session-table reads/writes run from problem state in the
** STC (single consumer).  No locking required in Phase 1.
**
** ufsd_sess_init   - allocate session table, store ptr in anchor
** ufsd_sess_free   - release session table
** ufsd_sess_open   - allocate slot, generate token, return token
** ufsd_sess_close  - validate token, release slot
** ufsd_sess_list   - WTO for each active session (SESSIONS cmd)
** ufsd_sess_find   - validate token, return UFSD_SESSION * or NULL
*/

#include "ufsd.h"
#include <string.h>
#include <stdlib.h>
#include <clibos.h>
#include <clibwto.h>
#include <cvt.h>
#include <ihaasvt.h>

/* Static serial counter.  Accessed only from the STC (problem state,
** single-threaded in Phase 1).  NOT in CSA; no key-0 window needed. */
static unsigned s_sess_serial = 0;

/* ============================================================
** ufsd_sess_init
**
** Allocate the session table in STC heap and store the pointer
** in the CSA anchor.  Called once at STC startup.
** Returns 0 on success, 8 on failure.
** ============================================================ */
int
ufsd_sess_init(UFSD_ANCHOR *anchor)
{
    UFSD_SESSION   *sessions;
    unsigned char   savekey;
    unsigned        i;
    int             j;

    if (!anchor) return 8;

    sessions = (UFSD_SESSION *)calloc(UFSD_MAX_SESSIONS,
                                      sizeof(UFSD_SESSION));
    if (!sessions) {
        wtof("UFSD045E Cannot allocate session table");
        return 8;
    }

    /* Initialise every slot to the unused state */
    for (i = 0; i < UFSD_MAX_SESSIONS; i++) {
        memcpy(sessions[i].eye, "UFSDSESS", 8);
        sessions[i].flags = 0;
        for (j = 0; j < UFSD_MAX_FD; j++) {
            sessions[i].fd_table[j].gfile_idx = UFSD_FD_UNUSED;
            sessions[i].fd_table[j].flags     = 0;
        }
    }

    /* Store pointer in CSA anchor (key-0 write) */
    if (__super(PSWKEY0, &savekey)) {
        free(sessions);
        wtof("UFSD046E Cannot enter supervisor state for session init");
        return 8;
    }
    anchor->sessions     = sessions;
    anchor->max_sessions = UFSD_MAX_SESSIONS;
    __prob(savekey, NULL);

    return 0;
}

/* ============================================================
** ufsd_sess_free
**
** Release the session table.  All active sessions are abandoned
** (no ufsfree in Step 1; no UFS handles exist yet).
** Called at STC shutdown before ufsd_csa_free.
** ============================================================ */
void
ufsd_sess_free(UFSD_ANCHOR *anchor)
{
    UFSD_SESSION   *sessions;
    unsigned char   savekey;
    unsigned        i;

    if (!anchor) return;

    /* Read and clear pointer in key-0 window */
    if (__super(PSWKEY0, &savekey)) return;
    sessions             = anchor->sessions;
    anchor->sessions     = NULL;
    anchor->max_sessions = 0;
    __prob(savekey, NULL);

    if (sessions) {
        /* Release any UFS handles left on active sessions */
        for (i = 0; i < UFSD_MAX_SESSIONS; i++) {
            if (sessions[i].flags & UFSD_SESS_ACTIVE) {
                int j;
                /* Release open file descriptors */
                for (j = 0; j < UFSD_MAX_FD; j++) {
                    if (sessions[i].fd_table[j].gfile_idx != UFSD_FD_UNUSED) {
                        ufsd_gft_release(anchor, sessions[i].fd_table[j].gfile_idx);
                        sessions[i].fd_table[j].gfile_idx = UFSD_FD_UNUSED;
                        sessions[i].fd_table[j].flags     = 0;
                    }
                }
                if (sessions[i].ufs) {
                    free(sessions[i].ufs);
                    sessions[i].ufs = NULL;
                }
            }
        }
        free(sessions);
    }
}

/* ============================================================
** ufsd_sess_find
**
** Validate token and return pointer to the active session slot,
** or NULL if the token is invalid or the slot is not active.
**
** Token layout: high 16 bits = (slot_index + 1), low 16 = serial.
** ============================================================ */
UFSD_SESSION *
ufsd_sess_find(UFSD_ANCHOR *anchor, unsigned token)
{
    unsigned      slot;
    UFSD_SESSION *sess;

    if (!anchor || !anchor->sessions || token == 0) return NULL;

    slot = (token >> 16);
    if (slot == 0 || slot > anchor->max_sessions) return NULL;
    slot--;                         /* convert to 0-based index */

    sess = &anchor->sessions[slot];
    if (!(sess->flags & UFSD_SESS_ACTIVE)) return NULL;
    if (sess->token != token) return NULL;

    return sess;
}

/* ============================================================
** ufsd_sess_open
**
** Allocate a free session slot, generate a token, and return
** the token via *out_token.  Called from ufsd_dispatch (problem
** state, STC address space); reads req->client_asid (CSA read
** is always permitted).
**
** Returns UFSD_RC_OK on success, UFSD_RC_NOREQ if no free slot.
** ============================================================ */
int
ufsd_sess_open(UFSD_ANCHOR *anchor, UFSREQ *req, unsigned *out_token)
{
    unsigned      i;
    unsigned      slot;
    UFSD_SESSION *sess;
    unsigned      token;
    UFSD_UFS     *ufs;
    int           j;

    if (!anchor || !anchor->sessions || !out_token) return UFSD_RC_CORRUPT;

    *out_token = 0;

    /* Find the first inactive slot */
    slot = anchor->max_sessions;               /* sentinel */
    for (i = 0; i < anchor->max_sessions; i++) {
        if (!(anchor->sessions[i].flags & UFSD_SESS_ACTIVE)) {
            slot = i;
            break;
        }
    }
    if (slot >= anchor->max_sessions) return UFSD_RC_NOREQ;

    /* Generate token */
    s_sess_serial++;
    token = ((slot + 1U) << 16) | (s_sess_serial & 0xFFFFU);

    /* Allocate per-session UFS handle */
    ufs = (UFSD_UFS *)calloc(1, sizeof(UFSD_UFS));
    if (!ufs) {
        wtof("UFSD070E Cannot allocate UFS handle");
        return UFSD_RC_NOREQ;
    }
    memcpy(ufs->eye, "UFSD_UFS", 8);
    ufs->flags    = 0;
    ufs->cwd[0]   = '/';
    ufs->cwd[1]   = '\0';
    ufs->cwd_ino  = UFSD_ROOT_INO;
    ufs->disk_idx = 0;

    sess = &anchor->sessions[slot];
    memcpy(sess->eye, "UFSDSESS", 8);
    sess->token        = token;
    sess->client_asid  = req ? req->client_asid : 0;
    sess->flags        = UFSD_SESS_ACTIVE;
    sess->ufs          = (void *)ufs;

    /* Owner/group start empty — set by SETUSER after SESS_OPEN */
    memset(sess->owner, 0, sizeof(sess->owner));
    memset(sess->group, 0, sizeof(sess->group));
    if (req && req->data_len >= 18U) {
        memcpy(sess->owner, req->data,     9);
        sess->owner[8] = '\0';
        memcpy(sess->group, req->data + 9, 9);
        sess->group[8] = '\0';
    }

    /* Initialise fd_table */
    for (j = 0; j < UFSD_MAX_FD; j++) {
        sess->fd_table[j].gfile_idx = UFSD_FD_UNUSED;
        sess->fd_table[j].flags     = 0;
    }

    *out_token = token;
    return UFSD_RC_OK;
}

/* ============================================================
** ufsd_sess_close
**
** Validate the session token in req->session_token and mark the
** slot inactive.  Called from ufsd_dispatch (problem state).
** Returns UFSD_RC_OK or UFSD_RC_BADSESS.
** ============================================================ */
int
ufsd_sess_close(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    UFSD_SESSION *sess;
    int           j;

    if (!anchor || !req) return UFSD_RC_CORRUPT;

    sess = ufsd_sess_find(anchor, req->session_token);
    if (!sess) return UFSD_RC_BADSESS;

    /* Release any still-open file descriptors */
    for (j = 0; j < UFSD_MAX_FD; j++) {
        if (sess->fd_table[j].gfile_idx != UFSD_FD_UNUSED) {
            ufsd_gft_release(anchor, sess->fd_table[j].gfile_idx);
            sess->fd_table[j].gfile_idx = UFSD_FD_UNUSED;
            sess->fd_table[j].flags     = 0;
        }
    }

    /* Free per-session UFS handle */
    if (sess->ufs) {
        free(sess->ufs);
        sess->ufs = NULL;
    }
    sess->flags = 0;
    sess->token = 0;

    return UFSD_RC_OK;
}

/* ============================================================
** ufsd_sess_setuser
**
** Update the session owner userid and optionally the group.
** Wire: req->data[0..8]  = userid (NUL-terminated, max 8 chars)
**       req->data[9..17] = group  (NUL-terminated, or zero = keep)
** data_len = 18.
** Returns UFSD_RC_OK, UFSD_RC_BADSESS, or UFSD_RC_INVALID.
** ============================================================ */
int
ufsd_sess_setuser(UFSD_ANCHOR *anchor, UFSREQ *req)
{
    UFSD_SESSION *sess;

    if (!anchor || !req) return UFSD_RC_CORRUPT;

    sess = ufsd_sess_find(anchor, req->session_token);
    if (!sess) return UFSD_RC_BADSESS;

    if (req->data_len != 18U) return UFSD_RC_INVALID;

    /* userid: data[0..8] — always set */
    memset(sess->owner, 0, sizeof(sess->owner));
    memcpy(sess->owner, req->data, 9);
    sess->owner[8] = '\0';

    /* group: data[9..17] — only update if non-empty (zero = keep default) */
    if (req->data[9] != '\0') {
        memset(sess->group, 0, sizeof(sess->group));
        memcpy(sess->group, req->data + 9, 9);
        sess->group[8] = '\0';
    }

    return UFSD_RC_OK;
}

/* ============================================================
** ufsd_sess_list
**
** Issue WTOs listing all active sessions.
** Called from the SESSIONS operator command handler.
** ============================================================ */
void
ufsd_sess_list(UFSD_ANCHOR *anchor)
{
    unsigned      i;
    unsigned      count;
    unsigned      fd_count;
    int           j;
    UFSD_SESSION *sess;

    if (!anchor || !anchor->sessions) {
        wtof("UFSD050I ACTIVE SESSIONS: 0");
        return;
    }

    count = 0;
    for (i = 0; i < anchor->max_sessions; i++) {
        if (anchor->sessions[i].flags & UFSD_SESS_ACTIVE) count++;
    }
    wtof("UFSD050I ACTIVE SESSIONS: %u", count);

    for (i = 0; i < anchor->max_sessions; i++) {
        sess = &anchor->sessions[i];
        if (!(sess->flags & UFSD_SESS_ACTIVE)) continue;

        fd_count = 0;
        for (j = 0; j < UFSD_MAX_FD; j++) {
            if (sess->fd_table[j].gfile_idx != UFSD_FD_UNUSED) fd_count++;
        }

        wtof("UFSD051I   #%u TOKEN=%08X ASID=%04X"
             " OWNER=%-8.8s GROUP=%-8.8s FDs=%u/%u",
             i + 1U,
             sess->token,
             sess->client_asid,
             sess->owner[0] ? sess->owner : "(none)",
             sess->group[0] ? sess->group : "(none)",
             fd_count,
             (unsigned)UFSD_MAX_FD);
    }
}

/* ============================================================
** ufsd_sess_cleanup
**
** Walk all active sessions.  For each one, check the ASVT to
** see whether the owning address space (client_asid) is still
** assigned.  If the ASID is no longer active, close the session
** (release FDs, GFT entries, UFS handle) and log a message.
**
** Returns the number of sessions cleaned up.
** ============================================================ */
unsigned
ufsd_sess_cleanup(UFSD_ANCHOR *anchor)
{
    CVT          *cvt;
    ASVT         *asvt;
    unsigned      i;
    unsigned      cleaned;
    unsigned      asid;
    unsigned      asvte;
    UFSD_SESSION *sess;
    int           j;

    if (!anchor || !anchor->sessions) return 0;

    cvt  = *(CVT **)16;
    asvt = (ASVT *)cvt->cvtasvt;
    if (!asvt) return 0;

    cleaned = 0;

    for (i = 0; i < anchor->max_sessions; i++) {
        sess = &anchor->sessions[i];
        if (!(sess->flags & UFSD_SESS_ACTIVE)) continue;

        asid = sess->client_asid;

        /* Validate ASID range */
        if (asid == 0 || asid > asvt->asvtmaxu)
            goto stale;

        /* Check ASVT entry: high bit set = ASID available (not assigned) */
        asvte = *(unsigned *)&asvt->asvtenty[asid - 1];
        if (asvte & 0x80000000U)
            goto stale;

        continue;  /* address space still active */

    stale:
        /* Release open file descriptors */
        for (j = 0; j < UFSD_MAX_FD; j++) {
            if (sess->fd_table[j].gfile_idx != UFSD_FD_UNUSED) {
                ufsd_gft_release(anchor, sess->fd_table[j].gfile_idx);
                sess->fd_table[j].gfile_idx = UFSD_FD_UNUSED;
                sess->fd_table[j].flags     = 0;
            }
        }

        /* Free per-session UFS handle */
        if (sess->ufs) {
            free(sess->ufs);
            sess->ufs = NULL;
        }

        wtof("UFSD052I CLEANUP: session #%u TOKEN=%08X ASID=%04X"
             " (%.8s) released",
             i + 1U, sess->token, asid,
             sess->owner[0] ? sess->owner : "(none)");

        ufsd_trace(anchor, UFSD_T_SESS_ABEND, sess->token, 0);

        sess->flags = 0;
        sess->token = 0;
        cleaned++;
    }

    return cleaned;
}
