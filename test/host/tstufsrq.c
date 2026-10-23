/* TSTUFSRQ.C - the request queue loses no request (#159)
**
** Host-only.  Links the real src/ufsd#rq.c.  The machine-dependent steps
** under it (src/ufsd#rqc.c on the target) are stand-ins here, and they are
** where another CPU gets to run: that is the whole race.
**
** The case that was lost: the queue holds one request, A.  The server pops
** it and, between its two updates of the queue words, a client appends B.
** B is linked behind A, which the server already took, and the head stays
** empty: the server never finds B.  Run deterministically, then as a seeded
** random run in which a client pushes at every point where it can, and
** every request pushed must come out once, in the order it went in.
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
** The stand-ins.  The other CPU runs inside ufsd_rq_window(): g_once exactly
** once (the next window), g_noise at every window.  Both are off while it
** runs, so its own pushes do not recurse.
** ------------------------------------------------------------ */
static void (*g_once)(void);
static void (*g_noise)(void);

void ufsd_rq_lock(UFSD_ANCHOR *anchor)   { (void)anchor; }
void ufsd_rq_unlock(UFSD_ANCHOR *anchor) { (void)anchor; }

void
ufsd_rq_window(void)
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
    return ufsd_rq_pop(&g_anchor);
}

/* ------------------------------------------------------------
** The client's append that lands in the server's window.
** ------------------------------------------------------------ */
static UFSREQ *g_b;

static void
client_pushes_b(void)
{
    g_b = new_req();
    push(g_b);
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
    CHECK(push(a) == 1, "first push reports an empty queue");
    CHECK(push(b) == 0, "second push reports a queue in use");
    CHECK(push(c) == 0, "third push reports a queue in use");
    CHECK(pop() == a && pop() == b && pop() == c, "popped in the order pushed");
    CHECK(a->next == NULL && b->next == NULL && c->next == NULL,
          "a popped request's chain word is cleared");
    CHECK(pop() == NULL, "queue empty again");
    CHECK(push(a) == 1, "an emptied queue reports empty on the next push");
    CHECK(pop() == a && pop() == NULL, "and works again");
}

static void
test_client_pushes_while_server_pops_the_last(void)
{
    UFSREQ *a, *got_a, *got_b, *more;

    fresh_queue();
    a = new_req();
    push(a);

    g_once = client_pushes_b;
    got_a  = pop();
    CHECK(got_a == a, "race: the server took A");
    CHECK(g_b == &g_req[1], "race: the client pushed B meanwhile");

    got_b = pop();
    CHECK(got_b == g_b, "race: B is not lost -- the server finds it");
    more = pop();
    CHECK(more == NULL, "race: nothing else on the queue");

    /* and the queue still works: the client behind B is not lost either */
    a = new_req();
    push(a);
    CHECK(pop() == a, "race: a later request is found too");
    CHECK(pop() == NULL, "race: queue empty at the end");
}

/* ------------------------------------------------------------
** Seeded random run.  Every push gets the next number; the pops must
** return the numbers in the same order, none twice, none missing.
** ------------------------------------------------------------ */
static unsigned g_seed;
static unsigned g_pushed;               /* requests pushed so far           */
static unsigned g_popped;               /* requests popped so far           */
static unsigned g_violations;
static unsigned g_pushed_in_window;

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
    if (r != &g_req[g_popped]) g_violations++;      /* out of order, twice */
    g_popped++;
}

static void
random_client(void)
{
    if (rnd() % 2) {
        if (push_next()) g_pushed_in_window++;
    }
}

static void
test_random_interference(void)
{
    unsigned round, i;
    unsigned lost = 0;

    g_violations       = 0;
    g_pushed_in_window = 0;
    for (round = 0; round < 200; round++) {
        g_seed = round * 7919U + 1U;
        fresh_queue();
        g_pushed = 0;
        g_popped = 0;
        g_noise  = random_client;
        for (i = 0; i < 100; i++) {
            if (rnd() % 3 == 0) push_next();
            else                pop_checked();
        }
        g_noise = NULL;
        for (i = 0; i < NREQ; i++)
            pop_checked();
        if (g_popped != g_pushed) lost++;
    }
    CHECK(g_violations == 0, "random: pops come out in the order of the pushes");
    CHECK(lost == 0, "random: every request pushed is popped");
    CHECK(g_pushed_in_window > 100U,
          "random: the other CPU did push inside the server's window");
}

int
main(void)
{
    test_fifo();
    test_client_pushes_while_server_pops_the_last();
    test_random_interference();
    return mbt_test_summary("TSTUFSRQ");
}
