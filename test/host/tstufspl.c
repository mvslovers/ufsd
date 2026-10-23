/* TSTUFSPL.C - the CSA free pools pop and push with CDS, not a head-only CS (#142)
**
** Host-only.  Links the real src/ufsd#pol.c; ufsd_pool_cds() -- the CDS
** instruction on the target -- is a stand-in here, and it is where another
** CPU gets to run: before it compares, it can pop and push on the same pool.
** That is the whole race.  A pop reads the head, then the successor of the
** head, then swaps; anything another CPU does in between is invisible to a
** CS on the head word alone.
**
** The ABA case (pop A, pop B, push A, then the old swap) is run twice:
**
**   control   a pop that compares the head word only, as ufsd_buf_alloc and
**             free_pop did before #142.  The swap succeeds, B -- owned by the
**             other CPU -- becomes the head, and one block has two owners.
**             This case is here to show that the scenario is a real ABA, not
**             to test product code: if it ever stops reproducing, the cases
**             below prove nothing.
**   product   ufsd_pool_pop with the same interleaving.  The tag moved, the
**             swap fails, the pop starts over and takes A with its current
**             successor.  B stays with its owner.
**
** Then a seeded random run: every compare is preceded, at random, by a pop or
** a push of another "CPU", and after every operation each item is either on
** the chain or held by exactly one owner -- never both, never twice.
**
** The fixtures are file statics: this is a test, not an AC(1) module.
*/

#include <mbtcheck.h>
#include <stddef.h>
#include <string.h>
#include "ufsd.h"

/* An item with its chain word away from offset 0, like UFSREQ (eye first). */
typedef struct item ITEM;
struct item {
    char   eye[8];
    ITEM  *next;
    int    id;
};

#define LINK_OFF  ((unsigned)offsetof(ITEM, next))
#define NITEMS    8

static ITEM    g_items[NITEMS];
static UFSPOOL g_pool;

/* ------------------------------------------------------------
** The CDS stand-in.  The other CPU runs before the compare: g_once
** exactly once (the next compare), g_noise before every compare.  Both
** are off while it runs, so its own pops and pushes do not recurse.
** ------------------------------------------------------------ */
static void (*g_once)(void);
static void (*g_noise)(void);
static unsigned g_cds_calls;
static unsigned g_cds_fail;

int
ufsd_pool_cds(UFSPOOL *pool, UFSPOOL *expect, void *new_head, unsigned new_tag)
{
    void (*once)(void)  = g_once;
    void (*noise)(void) = g_noise;

    g_cds_calls++;
    g_once  = NULL;
    g_noise = NULL;
    if (once != NULL)
        once();
    else if (noise != NULL)
        noise();
    g_noise = noise;
    if (pool->head == expect->head && pool->tag == expect->tag) {
        pool->head = new_head;
        pool->tag  = new_tag;
        return 0;
    }
    g_cds_fail++;
    expect->head = pool->head;
    expect->tag  = pool->tag;
    return 1;
}

/* ------------------------------------------------------------
** Helpers
** ------------------------------------------------------------ */
static void
fresh_pool(unsigned n, unsigned tag)
{
    unsigned i;

    memset(g_items, 0, sizeof g_items);
    for (i = 0; i < NITEMS; i++) {
        memcpy(g_items[i].eye, "ITEM____", 8);
        g_items[i].id = (int)i;
    }
    g_pool.head = NULL;
    g_pool.tag  = tag;
    for (i = n; i > 0; i--) {           /* chain 0 -> 1 -> ... -> n-1 */
        g_items[i - 1].next = (ITEM *)g_pool.head;
        g_pool.head         = &g_items[i - 1];
    }
    g_once      = NULL;
    g_noise     = NULL;
    g_cds_calls = 0;
    g_cds_fail  = 0;
}

/* How many times is item i on the chain?  The walk is capped, so a loop
** made by a bad pop ends the walk instead of the test. */
static unsigned
on_chain(const ITEM *it)
{
    const ITEM *p;
    unsigned    n = 0;
    unsigned    steps = 0;

    for (p = (const ITEM *)g_pool.head; p != NULL && steps < 4 * NITEMS;
         p = p->next, steps++)
        if (p == it) n++;
    return n;
}

static unsigned
chain_len(void)
{
    const ITEM *p;
    unsigned    n = 0;

    for (p = (const ITEM *)g_pool.head; p != NULL && n < 4 * NITEMS; p = p->next)
        n++;
    return n;
}

static ITEM *
pop(void)
{
    return (ITEM *)ufsd_pool_pop(&g_pool, LINK_OFF);
}

static void
push(ITEM *it)
{
    ufsd_pool_push(&g_pool, it, LINK_OFF);
}

/* ------------------------------------------------------------
** The other CPU in the ABA case: takes A and B, gives A back.
** ------------------------------------------------------------ */
static ITEM *g_other_a;
static ITEM *g_other_b;

static void
other_pops_two_pushes_first(void)
{
    g_other_a = pop();
    g_other_b = pop();
    push(g_other_a);
}

/* ------------------------------------------------------------
** Control: the pop this issue replaces, head word only.
** ------------------------------------------------------------ */
static ITEM *
legacy_pop(void (*between)(void))
{
    ITEM *item;
    ITEM *nxt;

    for (;;) {
        item = (ITEM *)g_pool.head;
        if (item == NULL) return NULL;
        nxt = item->next;
        if (between != NULL) {          /* the other CPU, once */
            between();
            between = NULL;
        }
        if (g_pool.head == (void *)item) {      /* CS on the head word */
            g_pool.head = nxt;
            item->next  = NULL;
            return item;
        }
    }
}

static void
test_control_head_only_cs_has_two_owners(void)
{
    ITEM *mine;

    fresh_pool(3, 0);                   /* A -> B -> C */
    mine = legacy_pop(other_pops_two_pushes_first);

    CHECK(mine == &g_items[0], "control: legacy pop took A");
    CHECK(g_other_b == &g_items[1], "control: the other CPU holds B");
    CHECK(g_pool.head == (void *)&g_items[1],
          "control: B, held by the other CPU, is the head of the free chain");
    CHECK(on_chain(&g_items[1]) == 1,
          "control: B is on the free chain while it is in use (two owners)");
}

/* ------------------------------------------------------------
** Product: the same interleaving through ufsd_pool_pop.
** ------------------------------------------------------------ */
static void
test_aba_pop_is_refused(void)
{
    ITEM    *mine;
    unsigned tag0;

    fresh_pool(3, 0);                   /* A -> B -> C */
    tag0   = g_pool.tag;
    g_once = other_pops_two_pushes_first;
    mine   = pop();

    CHECK(mine == &g_items[0], "ABA: the pop took A");
    CHECK(g_other_b == &g_items[1], "ABA: the other CPU still holds B");
    CHECK(on_chain(&g_items[1]) == 0, "ABA: B is not on the free chain");
    CHECK(on_chain(&g_items[0]) == 0, "ABA: A is not on the free chain");
    CHECK(g_pool.head == (void *)&g_items[2], "ABA: C is the head");
    CHECK(chain_len() == 1, "ABA: one block left free");
    CHECK(mine->next == NULL, "ABA: the popped block's chain word is cleared");
    CHECK(g_cds_fail == 1, "ABA: exactly one swap was refused");
    CHECK(g_pool.tag == tag0 + 4U,
          "ABA: tag moved once per successful pop and push (3 by the other"
          " CPU, 1 by the pop)");
}

/* A push needs the tag for no safety of its own (it links to whatever the
** head is when the swap succeeds), but it must still survive a head that
** changed under it and come back. */
static ITEM *g_push_victim;

static void
other_pops_head(void)
{
    g_push_victim = pop();
}

static void
test_push_retries_after_head_moved(void)
{
    fresh_pool(3, 0);                   /* 0 -> 1 -> 2 */
    g_push_victim = NULL;
    {
        ITEM *x = pop();                /* x = item 0; chain 1 -> 2 */

        g_once = other_pops_head;       /* takes item 1 before the swap */
        push(x);
    }
    CHECK(g_push_victim == &g_items[1], "push: the other CPU took item 1");
    CHECK(g_pool.head == (void *)&g_items[0], "push: item 0 is the head");
    CHECK(g_items[0].next == &g_items[2], "push: item 0 links to item 2");
    CHECK(chain_len() == 2, "push: two blocks free");
    CHECK(g_cds_fail == 1, "push: the first swap was refused");
}

static void
test_lifo_and_tag(void)
{
    ITEM *a;
    ITEM *b;
    ITEM *c;

    fresh_pool(0, 0);
    CHECK(pop() == NULL, "empty pool pops NULL");
    CHECK(g_pool.tag == 0, "empty pop leaves the tag alone");
    CHECK(g_cds_calls == 0, "empty pop does not swap");

    push(&g_items[0]);
    push(&g_items[1]);
    push(&g_items[2]);
    CHECK(g_pool.tag == 3U, "tag counts pushes");

    c = pop();
    b = pop();
    a = pop();
    CHECK(c == &g_items[2] && b == &g_items[1] && a == &g_items[0],
          "pops come back last in, first out");
    CHECK(g_pool.tag == 6U, "tag counts pops");
    CHECK(c->next == NULL && b->next == NULL && a->next == NULL,
          "popped blocks carry no chain word");
    CHECK(pop() == NULL && g_pool.head == NULL, "pool empty again");
}

static void
test_tag_wraps(void)
{
    ITEM *x;

    fresh_pool(2, 0xFFFFFFFFU);
    x = pop();
    CHECK(x == &g_items[0], "wrap: pop works with tag 0xFFFFFFFF");
    CHECK(g_pool.tag == 0, "wrap: the tag wrapped to 0");
    push(x);
    CHECK(g_pool.tag == 1U, "wrap: push counts on from 0");
    CHECK(chain_len() == 2, "wrap: both blocks free");
}

/* ------------------------------------------------------------
** Seeded random interference.  OWNER[i]: 0 = free (on chain),
** 1 = held by "this CPU", 2 = held by "the other CPU".
** ------------------------------------------------------------ */
static int      g_owner[NITEMS];
static unsigned g_seed;
static unsigned g_violations;

static unsigned
rnd(void)
{
    g_seed = g_seed * 1103515245U + 12345U;
    return (g_seed >> 16) & 0x7FFFU;
}

static void
audit(void)
{
    unsigned i;

    for (i = 0; i < NITEMS; i++) {
        unsigned n = on_chain(&g_items[i]);

        if (g_owner[i] == 0 && n != 1) g_violations++;      /* lost or doubled */
        if (g_owner[i] != 0 && n != 0) g_violations++;      /* two owners */
    }
}

static void
other_cpu_body(void)
{
    unsigned i;

    switch (rnd() % 3) {
    case 0:                             /* pop one */
        {
            ITEM *it = pop();

            if (it != NULL) {
                if (g_owner[it->id] != 0) g_violations++;
                g_owner[it->id] = 2;
            }
        }
        break;
    case 1:                             /* push one it holds */
        for (i = 0; i < NITEMS; i++)
            if (g_owner[i] == 2) {
                g_owner[i] = 0;
                push(&g_items[i]);
                break;
            }
        break;
    default:                            /* pop two, push the first back: ABA */
        {
            ITEM *a = pop();
            ITEM *b = pop();

            if (a != NULL) {
                if (g_owner[a->id] != 0) g_violations++;
                g_owner[a->id] = 2;
            }
            if (b != NULL) {
                if (g_owner[b->id] != 0) g_violations++;
                g_owner[b->id] = 2;
            }
            if (a != NULL) {
                g_owner[a->id] = 0;
                push(a);
            }
        }
        break;
    }
}

/* Before a compare: only some of them are interfered with, or no retry
** would ever get through. */
static void
other_cpu(void)
{
    if ((rnd() & 3U) == 0) other_cpu_body();
}

/* Between operations, so it keeps working while this CPU does no swap at
** all (an empty pool, nothing to push). */
static void
other_cpu_between(void)
{
    void (*noise)(void) = g_noise;

    g_noise = NULL;
    other_cpu_body();
    g_noise = noise;
}

static void
test_random_interference(void)
{
    unsigned round;
    unsigned i;
    unsigned pops = 0;
    unsigned pushes = 0;

    fresh_pool(NITEMS, 0);
    for (i = 0; i < NITEMS; i++) g_owner[i] = 0;
    g_seed       = 20260610U;
    g_violations = 0;
    g_noise      = other_cpu;

    for (round = 0; round < 20000U; round++) {
        if (rnd() & 1U) {
            ITEM *it = pop();

            if (it != NULL) {
                if (g_owner[it->id] != 0) g_violations++;
                g_owner[it->id] = 1;
                pops++;
            }
        } else {
            for (i = 0; i < NITEMS; i++)
                if (g_owner[i] == 1) {
                    g_owner[i] = 0;
                    push(&g_items[i]);
                    pushes++;
                    break;
                }
        }
        audit();
        if (rnd() & 1U) {
            other_cpu_between();
            audit();
        }
    }
    g_noise = NULL;

    CHECK(g_violations == 0,
          "random: no block lost, doubled or owned twice in 20000 rounds");
    CHECK(g_cds_fail > 1000U,
          "random: the interference really made the swap fail (>1000 times)");
    CHECK(pops > 1000U && pushes > 1000U,
          "random: the run did both pops and pushes");
}

int
main(void)
{
    test_lifo_and_tag();
    test_tag_wraps();
    test_control_head_only_cs_has_two_owners();
    test_aba_pop_is_refused();
    test_push_retries_after_head_moved();
    test_random_interference();
    return mbt_test_summary("TSTUFSPL");
}
