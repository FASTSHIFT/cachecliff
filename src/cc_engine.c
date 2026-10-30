/**
 * @file cc_engine.c
 * @brief The dispatcher, the timing and the validation.
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cachecliff.h"

/** @brief The data region the callbacks read and write. */
static CC_DATA_ATTR uint32_t g_data[CC_DATA_BYTES / 4u];

/** @brief Where the callbacks' results go, so that the compiler keeps them. */
static volatile uint32_t g_sink;

/** @brief Least time a measurement takes, in milliseconds. */
static uint32_t g_target_ms = CC_TARGET_MS;

uint32_t *cc_data(void) { return g_data; }

void cc_set_target_ms(uint32_t ms) { g_target_ms = ms ? ms : 1u; }

uint32_t cc_xorshift(uint32_t s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/**
 * @brief The dispatcher's draw, inlined into the run loop.
 *
 * tools/ccgen.py has the same function to compute the validation values.
 *
 * @param[in,out] state Generator state.
 * @param[in] n Number of callbacks to pick from.
 * @param[in] skew At least 1; see cc_cfg::skew.
 * @return An index in <tt>[0, n)</tt>.
 */
static inline uint32_t draw(uint32_t *state, uint32_t n, uint32_t skew)
{
    uint32_t s = cc_xorshift(*state);
    uint32_t j = (uint32_t)(((uint64_t)s * n) >> 32);
    while (--skew) {
        s = cc_xorshift(s);
        uint32_t j2 = (uint32_t)(((uint64_t)s * n) >> 32);
        if (j2 < j)
            j = j2;
    }
    *state = s;
    return j;
}

uint32_t cc_draw(uint32_t *state, uint32_t n, uint32_t skew)
{
    return draw(state, n, skew ? skew : 1u);
}

void cc_data_fill(uint32_t *d, uint32_t words, uint32_t seed)
{
    uint32_t s = seed | 1u;
    for (uint32_t i = 0; i < words; i++) {
        s = cc_xorshift(s);
        d[i] = s;
    }
}

uint32_t cc_run(const cc_cfg *cfg, uint32_t calls, uint32_t seed, uint32_t x0)
{
    cc_ctx c;
    c.data = g_data;
    c.mask = cfg->data_bytes / 4u - 1u;
    const uint32_t n = cfg->nfuncs;
    const uint32_t skew = cfg->skew ? cfg->skew : 1u;
    const uint32_t period = cfg->period;
    uint32_t state = seed | 1u;
    uint32_t left = period;
    uint32_t x = x0;

    for (uint32_t k = 0; k < calls; k++) {
        if (period) {
            if (left == 0) {
                state = seed | 1u;
                left = period;
            }
            left--;
        }
        x = cc_table[draw(&state, n, skew)](x, &c);
    }
    return x;
}

/**
 * @brief Ticks a measurement should take.
 * @return CC_TARGET_MS (or what cc_set_target_ms() set) in port ticks.
 */
static uint32_t target_ticks(void)
{
    return (uint32_t)(((uint64_t)cc_port_ticks_per_sec() * g_target_ms) / 1000u);
}

/**
 * @brief Times one run.
 * @param[in] cfg What to run.
 * @param[in] calls How many callbacks to call.
 * @param[in,out] x Value passed in and out of the run.
 * @return Ticks the run took.
 */
static uint32_t time_calls(const cc_cfg *cfg, uint32_t calls, uint32_t *x)
{
    uint32_t t0 = cc_port_ticks();
    *x = cc_run(cfg, calls, 12345u, *x);
    return cc_port_ticks() - t0;
}

void cc_median3(uint32_t *a)
{
    uint32_t t;
    if (a[0] > a[1]) {
        t = a[0];
        a[0] = a[1];
        a[1] = t;
    }
    if (a[1] > a[2]) {
        t = a[1];
        a[1] = a[2];
        a[2] = t;
    }
    if (a[0] > a[1]) {
        t = a[0];
        a[0] = a[1];
        a[1] = t;
    }
}

uint32_t cc_scale_calls(uint32_t calls, uint32_t ticks, uint32_t target)
{
    uint64_t n = ticks ? ((uint64_t)calls * target) / ticks : (uint64_t)calls * 8u;
    if (n < 256u)
        n = 256u;
    if (n > (1u << 28))
        n = 1u << 28;
    return (uint32_t)n;
}

cc_result cc_measure(const cc_cfg *cfg)
{
    cc_result r;
    const uint32_t target = target_ticks();
    uint32_t x = 1u;
    uint32_t calls = 256u;
    uint32_t t = 0;

    cc_data_fill(g_data, cfg->data_bytes / 4u, 1u);

    /* a pilot run, long enough to be above the timer's resolution */
    for (;;) {
        t = time_calls(cfg, calls, &x);
        if (t >= target / 8u || calls >= CC_PILOT_MAX_CALLS)
            break;
        calls *= 4u;
    }
    calls = cc_scale_calls(calls, t, target);

    /* warm up; if the run is far from the target, again with a better number */
    for (int attempt = 0; attempt < 3; attempt++) {
        t = time_calls(cfg, calls, &x);
        if (t >= target / 2u && t <= target * 2u)
            break;
        calls = cc_scale_calls(calls, t, target);
    }

    uint32_t runs[3];
    for (int i = 0; i < 3; i++)
        runs[i] = time_calls(cfg, calls, &x);
    cc_median3(runs);

    g_sink ^= x;
    r.ticks = runs[1];
    r.calls = calls;
    r.sink = x;
    return r;
}

/**
 * @brief Links the nodes of the data region into one random cycle.
 *
 * A node is 64 bytes (16 words); word 0 is the number of the next node.
 * Sattolo's shuffle gives a single cycle, so that the walk visits every node
 * before it returns.
 *
 * @param[in] nodes Number of nodes, at least 2.
 */
static void chase_build(uint32_t nodes)
{
    uint32_t s = 0x2545F491u;
    for (uint32_t i = 0; i < nodes; i++)
        g_data[i << 4] = i;
    for (uint32_t i = nodes - 1u; i > 0; i--) {
        s = cc_xorshift(s);
        uint32_t j = s % i;
        uint32_t t = g_data[i << 4];
        g_data[i << 4] = g_data[j << 4];
        g_data[j << 4] = t;
    }
}

uint32_t cc_chase(uint32_t steps, uint32_t *sink)
{
    /* volatile: the loads cannot be dropped, folded, or moved past the timer reads */
    const volatile uint32_t *d = g_data;
    uint32_t p = 0;
    uint32_t t0 = cc_port_ticks();
    for (uint32_t k = 0; k < steps; k++)
        p = d[p << 4];
    uint32_t t = cc_port_ticks() - t0;
    *sink = p;
    return t;
}

uint32_t cc_chase_ticks_per_load_x1000(uint32_t bytes)
{
    const uint32_t target = target_ticks();
    uint32_t nodes = bytes / 64u;
    uint32_t sink = 0, steps = 1024u, t;

    if (nodes < 2u)
        nodes = 2u;
    chase_build(nodes);
    for (;;) {
        t = cc_chase(steps, &sink);
        if (t >= target / 8u || steps >= CC_PILOT_MAX_CALLS)
            break;
        steps *= 4u;
    }
    steps = cc_scale_calls(steps, t, target);
    (void)cc_chase(steps, &sink); /* warm up */
    uint32_t runs[3];
    for (int i = 0; i < 3; i++)
        runs[i] = cc_chase(steps, &sink);
    cc_median3(runs);
    g_sink ^= sink;
    return (uint32_t)(((uint64_t)runs[1] * 1000u) / steps);
}

int cc_validate_with(const cc_fn *table, uint32_t nfuncs, uint32_t bank_nfuncs,
    const uint32_t *expect_all, uint32_t expect_draw)
{
    int bad = 0;
    cc_ctx c;
    const uint32_t words = 4096u;

    if (bank_nfuncs < nfuncs)
        return 1000; /* the table is bigger than the bank it was generated with */

    c.data = g_data;
    c.mask = words - 1u;
    uint32_t k = 0;
    for (uint32_t n = 64u; n <= nfuncs; n <<= 1, k++) {
        cc_data_fill(g_data, words, 1u);
        uint32_t x = 1u;
        for (uint32_t i = 0; i < n; i++)
            x = table[i](x, &c);
        if (x != expect_all[k])
            bad++;
    }

    /* 2000 random calls among the first 64, skew 2; same as cc_run() with table */
    cc_data_fill(g_data, words, 1u);
    uint32_t state = 1u, x = 1u;
    for (uint32_t k2 = 0; k2 < 2000u; k2++)
        x = table[draw(&state, 64u, 2u)](x, &c);
    if (x != expect_draw)
        bad++;
    return bad;
}

int cc_validate(void)
{
    return cc_validate_with(cc_table, CC_NFUNCS, cc_bank_nfuncs, cc_validate_all, cc_validate_draw);
}
