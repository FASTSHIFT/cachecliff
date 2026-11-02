/**
 * @file cc_soak.c
 * @brief The soak loop; see cc_soak.h.
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cc_soak.h"
#include "cachecliff.h"
#include CC_CMSIS_HEADER
#include <stdio.h>
/** @brief Batches between two status lines. */
#ifndef CC_SOAK_STATUS_BATCHES
#define CC_SOAK_STATUS_BATCHES 256u
#endif
volatile cc_soak_ctl g_cc_soak = {
    .nfuncs = CC_NFUNCS,
    .skew = 1,
    .period = 0,
    .data_bytes = 4096,
    .batch = 20000,
    .caches = 0,
    .mode = CC_SOAK_CALLS,
};
volatile cc_soak_stat g_cc_stat;
/**
 * @brief Turns the caches on or off to match a request.
 *
 * Turning the data cache off cleans it first (SCB_DisableDCache() does), which
 * a write-back cache needs.
 *
 * @param[in] want The caches wanted.
 * @param[in] have The caches that are on now.
 */
static void apply_caches(uint32_t want, uint32_t have)
{
    if ((want ^ have) & CC_SOAK_ICACHE) {
        if (want & CC_SOAK_ICACHE)
            SCB_EnableICache();
        else
            SCB_DisableICache();
    }
    if ((want ^ have) & CC_SOAK_DCACHE) {
        if (want & CC_SOAK_DCACHE)
            SCB_EnableDCache();
        else
            SCB_DisableDCache();
    }
}
/**
 * @brief Prints the average of a configuration so far.
 * @param[in] c The configuration.
 * @param[in] calls Calls since it was set.
 * @param[in] ticks Their time in port ticks.
 */
static void print_status(const cc_soak_ctl *c, uint64_t calls, uint64_t ticks)
{
    char buf[128];
    const uint32_t x100 = calls ? (uint32_t)((ticks * 100u) / calls) : 0u;
    snprintf(buf, sizeof buf,
        "soak funcs=%u skew=%u period=%u data=%u caches=%u calls=%u ticks/call=%u.%02u\n",
        (unsigned)c->nfuncs, (unsigned)c->skew, (unsigned)c->period, (unsigned)c->data_bytes,
        (unsigned)c->caches, (unsigned)calls, (unsigned)(x100 / 100u), (unsigned)(x100 % 100u));
    cc_port_puts(buf);
}
/**
 * @brief Reads g_cc_soak into a local copy and keeps it within what is linked in.
 * @param[out] want The copy.
 */
static void read_ctl(cc_soak_ctl *want)
{
    want->nfuncs = g_cc_soak.nfuncs;
    want->skew = g_cc_soak.skew;
    want->period = g_cc_soak.period;
    want->data_bytes = g_cc_soak.data_bytes;
    want->batch = g_cc_soak.batch;
    want->caches = g_cc_soak.caches;
    want->mode = g_cc_soak.mode;
    if (want->nfuncs < 2 || want->nfuncs > CC_NFUNCS)
        want->nfuncs = CC_NFUNCS;
    if (want->data_bytes < 1024 || want->data_bytes > CC_DATA_BYTES)
        want->data_bytes = CC_DATA_BYTES;
}
/**
 * @brief Whether two configurations are different enough to restart the totals.
 * @param[in] a One.
 * @param[in] b The other.
 * @return Non-zero if they differ in anything but the batch size.
 */
static int differ(const cc_soak_ctl *a, const cc_soak_ctl *b)
{
    return a->nfuncs != b->nfuncs || a->skew != b->skew || a->period != b->period
        || a->data_bytes != b->data_bytes || a->caches != b->caches || a->mode != b->mode;
}
void cc_soak(uint32_t caches)
{
    uint32_t have = caches;
    cc_soak_ctl cur = { 0 };
    uint64_t calls = 0, ticks = 0;
    uint32_t seed = 1, x = 1;
    g_cc_soak.caches = caches;
    cc_port_puts("soak: change g_cc_soak with the debugger; status lines follow\n");
    for (;;) {
        cc_soak_ctl want;
        read_ctl(&want);
        if (want.caches != have) {
            apply_caches(want.caches, have);
            have = want.caches;
        }
        if (differ(&cur, &want)) {
            cur = want;
            calls = 0;
            ticks = 0;
            g_cc_stat.batches = 0;
            cc_data_fill(cc_data(), cur.data_bytes / 4u, 1u);
        }
        if (cur.mode == CC_SOAK_CHASE) {
            /* builds the chain, then times the walk */
            g_cc_stat.chase_x1000 = cc_chase_ticks_per_load_x1000(cur.data_bytes);
            g_cc_stat.batches++;
            continue;
        }
        const cc_cfg cfg = { cur.nfuncs, cur.skew, cur.period, cur.data_bytes };
        const uint32_t t0 = cc_port_ticks();
        x = cc_run(&cfg, cur.batch, seed++, x);
        const uint32_t dt = cc_port_ticks() - t0;
        calls += cur.batch;
        ticks += dt;
        g_cc_stat.batches++;
        g_cc_stat.calls_lo = (uint32_t)calls;
        g_cc_stat.ticks_lo = (uint32_t)ticks;
        g_cc_stat.last_ticks = dt;
        g_cc_stat.last_calls = cur.batch;
        g_cc_stat.sink = x;
        if (g_cc_stat.batches % CC_SOAK_STATUS_BATCHES == 0)
            print_status(&cur, calls, ticks);
    }
}
