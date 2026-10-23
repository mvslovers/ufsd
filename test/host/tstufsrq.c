/* TSTUFSRQ.C - the request queue loses no request (#159)
**
** Host-only.  Links the real src/ufsd#rq.c.  The one machine-dependent step
** under it, the CS (src/ufsd#rqc.c on the target), is a stand-in here, and
** it is where another CPU gets to run: before it compares, it can push or
** take the chain.  That is the whole race.
**
** The case that was lost, when the queue was a head/tail pair changed by
** the clients under a lock and by the server without one: the queue holds
** one request, A.  The server takes it and, between its two updates of the
** queue words, a client appends B.  B is linked behind A, which the server
** already took, and the head stays empty: the server never finds B.  With
** the head alone as the shared word, every one of those interleavings ends
** in a CS that fails and a retry, so the cases below are the ones that
** used to be (or could have been) lost, plus the wake-up rule:
**
**   last      a client pushes while the server takes the last request
**   wake      the server empties the chain between a client's read and its
**             swap: the push must then report an empty chain, because the
**             client that finds it empty is the one that posts the server
**   aba       the same request block is taken, served, and pushed again
**             between a client's read and its swap
**   clients   two clients push at once
**
** Then a seeded random run in which another CPU pushes or takes at every
** CS, and every request pushed must come out once, in the order its push
** succeeded.
**
** The fixtures are file statics: this is a test, not an AC(1) module.
*/

#include <mbtcheck.h>
#include <stddef.h>
#include <string.h>
#include "ufsd.h"

#define NREQ 64

static UFSD_ANCHOR g_anchor;
static UFSREQ      g_req[NREQ];
static unsigned    g_next_req;          /* next unused entry of g_req       */

/* ------------------------------------------------------------
** The CS stand-in.  The other CPU runs before the compare: g_once exactly
** once (the next compare), g_noise before every compare.  Both are off while
** it runs, so its own pushes and takes do not recurse.  g_order is the order
** in which pushes succeeded, which is the order the server must see.
** ------------------------------------------------------------ */
static void (*g_once)(void);
static void (*g_noise)(void);
static UFSREQ *g_order[NREQ];
static unsigned g_norder;
static unsigned g_cs_fail;
static int      g_in_pop;               /* the CS belongs to ufsd_rq_pop    */

int
ufsd_rq_cs(UFSREQ **word, UFSREQ **expect, UFSREQ *new_val)
{
    void (*once)(void)  = g_once;
    void (*noise)(void) = g_noise;

    g_once  = NULL;
    g_noise = NULL;
    if (once != NULL)
        once();
    else if (noise != NULL)
        noise();
    g_noise = noise;
    if (*word == *expect) {
        *word = new_val;
        if (new_val != NULL && g_norder < NREQ)
            g_order[g_norder++] = new_val;
        return 0;
    }
    g_cs_fail++;
    *expect = *word;
    return 1;
}

/* ------------------------------------------------------------
** Helpers
** ------------------------------------------------------------ */
static void
fresh_queue(void)
{
    unsigned i;

    memset(&g_anchor, 0, sizeof g_anchor);
    memset(g_req, 0, sizeof g_req);
    for (i = 0; i < NREQ; i++) {
        memcpy(g_req[i].eye, "UFSREQ__", 8);
        g_req[i].func = i;              /* its number, for the checks       */
    }
    g_next_req = 0;
    g_once     = NULL;
    g_noise    = NULL;
    g_norder   = 0;
    g_cs_fail  = 0;
    g_in_pop   = 0;
}

static UFSREQ *
new_req(void)
{
    return &g_req[g_next_req++];
}

static int
push(UFSREQ *req)
{
    return ufsd_rq_push(&g_anchor, req);
}

static UFSREQ *
pop(void)
{
    UFSREQ *r;

    g_in_pop = 1;
    r = ufsd_rq_pop(&g_anchor);
    g_in_pop = 0;
    return r;
}

/* ------------------------------------------------------------
** The other CPU's moves
** ------------------------------------------------------------ */
static UFSREQ *g_b;
static UFSREQ *g_taken;
static int     g_push_rc;

static void
client_pushes_b(void)
{
    g_b = new_req();
    g_push_rc = push(g_b);
}

static void
server_takes_one(void)
{
    g_taken = pop();
}

/* ------------------------------------------------------------
** Cases
** ------------------------------------------------------------ */
static void
test_fifo(void)
{
    UFSREQ *a, *b, *c;

    fresh_queue();
    CHECK(pop() == NULL, "empty queue pops NULL");

    a = new_req();
    b = new_req();
    c = new_req();
    CHECK(push(a) == 1, "first push reports an empty chain");
    CHECK(push(b) == 0, "second push reports a chain in use");
    CHECK(push(c) == 0, "third push reports a chain in use");
    CHECK(pop() == a, "popped in the order pushed: a");
    CHECK(g_anchor.req_head == NULL, "the server took the whole chain at once");
    CHECK(pop() == b && pop() == c, "popped in the order pushed: b, c");
    CHECK(a->next == NULL && b->next == NULL && c->next == NULL,
          "a popped request's chain word is cleared");
    CHECK(pop() == NULL, "queue empty again");
    CHECK(g_cs_fail == 0, "no CS failed without another CPU");

    CHECK(push(a) == 1, "an emptied queue reports empty on the next push");
    CHECK(pop() == a && pop() == NULL, "and works again");

    /* The server holds b and c in req_ready while the head is empty: a push
    ** now finds an empty chain and posts the server, which is harmless. */
    push(a);
    push(b);
    CHECK(pop() == a, "ready: a taken, b waits in the server's list");
    CHECK(push(c) == 1, "ready: a push behind a taken batch reports empty");
    CHECK(pop() == b && pop() == c && pop() == NULL, "ready: b, then c");
}

static void
test_last(void)
{
    UFSREQ *a, *got_a, *got_b;

    fresh_queue();
    a = new_req();
    push(a);

    g_once = client_pushes_b;           /* before the server's swap */
    got_a  = pop();
    CHECK(g_b == &g_req[1], "last: the client pushed B meanwhile");
    CHECK(g_push_rc == 0, "last: B found A on the chain");
    CHECK(got_a == a, "last: the server took A first");
    CHECK(g_cs_fail == 1, "last: the server's swap failed once and went on");

    got_b = pop();
    CHECK(got_b == g_b, "last: B is not lost -- the server finds it");
    CHECK(pop() == NULL, "last: nothing else on the queue");

    /* after the swap: B arrives while the server serves A */
    fresh_queue();
    a = new_req();
    push(a);
    got_a = pop();
    client_pushes_b();
    CHECK(got_a == &g_req[0] && g_push_rc == 1,
          "last: B behind a taken A finds an empty chain");
    CHECK(pop() == g_b && pop() == NULL, "last: and is found");
}

static void
test_wake(void)
{
    UFSREQ *a, *p;

    fresh_queue();
    a = new_req();
    push(a);
    p = new_req();

    g_once = server_takes_one;          /* between P's read of A and its swap */
    g_push_rc = push(p);
    CHECK(g_taken == a, "wake: the server emptied the chain meanwhile");
    CHECK(g_push_rc == 1,
          "wake: P's push reports an empty chain, so P posts the server");
    CHECK(g_cs_fail == 1, "wake: P's swap failed once");
    CHECK(pop() == p && pop() == NULL, "wake: P is found");
}

static UFSREQ *g_aba_a;
static UFSREQ *g_aba_x;
static UFSREQ *g_aba_served[2];

static void
server_serves_both_and_a_returns(void)
{
    g_aba_served[0] = pop();            /* X: the chain A -> X is taken whole */
    g_aba_served[1] = pop();            /* A */
    push(g_aba_a);                      /* A is a new request: head = A again */
}

static void
test_aba(void)
{
    UFSREQ *p;

    fresh_queue();
    g_aba_x = new_req();
    g_aba_a = new_req();
    p       = new_req();
    push(g_aba_x);
    push(g_aba_a);                      /* chain: A -> X */

    /* P read the head, A.  Before its swap the server serves X and A, and A
    ** comes back as a new request: the head is A again, but not the A that
    ** had X behind it.  P links behind A itself, so it needs nothing from the
    ** old A->next: X, which was served, must not reappear. */
    g_once = server_serves_both_and_a_returns;
    push(p);
    CHECK(g_aba_served[0] == g_aba_x && g_aba_served[1] == g_aba_a,
          "aba: the server served X, then A");
    CHECK(g_cs_fail == 0, "aba: the head was A again, so P's swap went through");
    CHECK(pop() == g_aba_a && pop() == p && pop() == NULL,
          "aba: A (pushed again), then P, and X does not come back");
}

static void
test_clients(void)
{
    UFSREQ *p;

    fresh_queue();
    p = new_req();
    g_once = client_pushes_b;           /* the other client wins the swap */
    g_push_rc = 0;
    push(p);
    CHECK(g_b == &g_req[1], "clients: the other client pushed first");
    CHECK(g_cs_fail == 1, "clients: P's swap failed once");
    CHECK(pop() == g_b && pop() == p && pop() == NULL,
          "clients: both found, in the order their pushes succeeded");
}

/* ------------------------------------------------------------
** Seeded random run.  The pops must return the requests in the order the
** pushes succeeded (g_order), none twice, none missing.
** ------------------------------------------------------------ */
static unsigned g_seed;
static unsigned g_pushed;               /* requests pushed so far           */
static unsigned g_popped;               /* requests popped so far           */
static unsigned g_violations;
static unsigned g_pushed_in_window;
static unsigned g_popped_in_window;
static unsigned g_cs_fail_total;

static unsigned
rnd(void)
{
    g_seed = g_seed * 1103515245U + 12345U;
    return (g_seed >> 16) & 0x7FFFU;
}

static int
push_next(void)
{
    UFSREQ *r;

    if (g_next_req >= NREQ) return 0;
    r = new_req();
    g_pushed++;
    push(r);
    return 1;
}

static void
pop_checked(void)
{
    UFSREQ *r = pop();

    if (r == NULL) return;
    if (g_popped >= g_norder || r != g_order[g_popped])
        g_violations++;                             /* out of order, twice */
    g_popped++;
}

/* In the server's own swap only a client can be the other CPU; in a
** client's swap it is either a client or the server. */
static void
random_other_cpu(void)
{
    unsigned r = rnd() % 4;

    if (r < 2) {
        if (push_next()) g_pushed_in_window++;
    } else if (r == 2 && !g_in_pop) {
        g_popped_in_window++;
        pop_checked();
    }
}

static void
test_random_interference(void)
{
    unsigned round, i;
    unsigned lost = 0;

    g_violations       = 0;
    g_pushed_in_window = 0;
    g_popped_in_window = 0;
    g_cs_fail_total    = 0;
    for (round = 0; round < 200; round++) {
        g_seed = round * 7919U + 1U;
        fresh_queue();
        g_pushed = 0;
        g_popped = 0;
        g_noise  = random_other_cpu;
        for (i = 0; i < 100; i++) {
            if (rnd() % 3 == 0) push_next();
            else                pop_checked();
        }
        g_noise = NULL;
        for (i = 0; i < NREQ; i++)
            pop_checked();
        if (g_popped != g_pushed) lost++;
        g_cs_fail_total += g_cs_fail;
    }
    CHECK(g_violations == 0, "random: pops come out in the order of the pushes");
    CHECK(lost == 0, "random: every request pushed is popped");
    CHECK(g_pushed_in_window > 100U && g_popped_in_window > 100U,
          "random: the other CPU pushed and took inside the CS windows");
    CHECK(g_cs_fail_total > 100U, "random: swaps were refused and retried");
}

int
main(void)
{
    test_fifo();
    test_last();
    test_wake();
    test_aba();
    test_clients();
    test_random_interference();
    return mbt_test_summary("TSTUFSRQ");
}
