/**
 * @file cc_main.c
 * @brief The sweeps and the report.
 *
 * Four sweeps, each changing one thing and leaving the rest as in the hot
 * reference (a few callbacks, a small data region):
 *   - the code footprint: how many callbacks are picked from;
 *   - the data footprint: how big the region is, by the callbacks and by a
 *     pointer chase;
 *   - the skew of the choice, over all callbacks;
 *   - the predictability of the choice: random, or a short repeating sequence.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cachecliff.h"

#include <stdarg.h>
#include <stdio.h>

/** @brief Callbacks of the hot reference. */
#define HOT_FUNCS 16u

/** @brief Data of the hot reference, in bytes. */
#define HOT_DATA 4096u

/** @brief A point is "within" the hot value when it is below this, in percent. */
#define KNEE_PERCENT 125u

/** @brief Output switches, set by cc_main(). */
static int g_csv;

/**
 * @brief Prints a formatted line through the port.
 * @param[in] fmt A printf format. Nothing longer than 255 characters.
 */
static void out(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    cc_port_puts(buf);
}

/**
 * @brief Time of one call in hundredths of a nanosecond.
 * @param[in] r A measurement.
 * @return 100 * nanoseconds per call.
 */
static uint64_t ns100_of_call(const cc_result *r)
{
    const uint64_t hz = cc_port_ticks_per_sec();
    const uint64_t tpc_x1000 = ((uint64_t)r->ticks * 1000u) / r->calls;
    return (tpc_x1000 * 100000000u) / hz;
}

/**
 * @brief Time of one load in hundredths of a nanosecond.
 * @param[in] ticks_x1000 Ticks per load times 1000.
 * @return 100 * nanoseconds per load.
 */
static uint64_t ns100_of_load(uint32_t ticks_x1000)
{
    return ((uint64_t)ticks_x1000 * 100000000u) / cc_port_ticks_per_sec();
}

uint32_t cc_code_span(const cc_fn *table, uint32_t total, uint32_t n)
{
    const uintptr_t first = (uintptr_t)table[0] & ~(uintptr_t)1u; /* Thumb bit */
    if (n < total) {
        const uintptr_t next = (uintptr_t)table[n] & ~(uintptr_t)1u;
        return next > first ? (uint32_t)(next - first) : 0u;
    }
    const uintptr_t last = (uintptr_t)table[n - 1u] & ~(uintptr_t)1u;
    if (last <= first)
        return 0u;
    return (uint32_t)((uint64_t)(last - first) * n / (n - 1u));
}

void cc_fmt_kib(char *buf, uint32_t bytes)
{
    if (bytes == 0u) {
        snprintf(buf, 16, "?");
        return;
    }
    const uint32_t x10 = (uint32_t)(((uint64_t)bytes * 10u) / 1024u);
    snprintf(buf, 16, "%u.%u", (unsigned)(x10 / 10u), (unsigned)(x10 % 10u));
}

/**
 * @brief Prints a ratio given in hundredths.
 * @param[out] buf At least 16 bytes.
 * @param[in] x100 100 * the ratio.
 */
static void ratio(char *buf, uint64_t x100)
{
    snprintf(buf, 16, "%u.%02ux", (unsigned)(x100 / 100u), (unsigned)(x100 % 100u));
}

/**
 * @brief Times a configuration and returns the time of a call.
 * @param[in] cfg What to run.
 * @return 100 * nanoseconds per call.
 */
static uint64_t time_cfg(const cc_cfg *cfg)
{
    const cc_result r = cc_measure(cfg);
    return ns100_of_call(&r);
}

/**
 * @brief Gets the machine into a steady state before anything is measured.
 *
 * A CPU that was idle runs slow until its clock has ramped up, and the first
 * touch of a page costs a fault. Both would make the first point of a sweep,
 * the one every other is compared with, slower than the points after it.
 */
static void warm_up(void)
{
    const cc_cfg cfg = { HOT_FUNCS, 1u, 0u, HOT_DATA };
    for (int i = 0; i < 4; i++)
        (void)cc_measure(&cfg);
    (void)cc_chase_ticks_per_load_x1000(HOT_DATA);
}

/**
 * @brief The hot reference: a few callbacks and a small data region.
 * @return 100 * nanoseconds per call; the faster of two measurements.
 */
static uint64_t hot_call(void)
{
    const cc_cfg cfg = { HOT_FUNCS, 1u, 0u, HOT_DATA };
    const uint64_t a = time_cfg(&cfg), b = time_cfg(&cfg);
    return a < b ? a : b;
}

/**
 * @brief The reference of the pointer chase: a region that fits any L1 cache.
 * @return 100 * nanoseconds per load; the faster of two measurements.
 */
static uint64_t hot_chase(void)
{
    const uint64_t a = ns100_of_load(cc_chase_ticks_per_load_x1000(HOT_DATA));
    const uint64_t b = ns100_of_load(cc_chase_ticks_per_load_x1000(HOT_DATA));
    return a < b ? a : b;
}

/**
 * @brief The code footprint sweep.
 * @param[in] hot Time of a call in the hot reference, from hot_call().
 * @param[out] knee_span Largest span within KNEE_PERCENT of the hot time.
 * @param[out] worst_x100 Slowdown at all callbacks, times 100.
 */
static void sweep_code(uint64_t hot, uint32_t *knee_span, uint64_t *worst_x100)
{
    cc_cfg cfg = { HOT_FUNCS, 1u, 0u, HOT_DATA };
    int within = 1;
    char a[16], b[16];

    if (!g_csv)
        out("\ncode footprint (uniform choice, %u KiB of data)\n  funcs   code KiB     ns/call   "
            "vs hot\n",
            (unsigned)(HOT_DATA / 1024u));
    *knee_span = 0;
    for (uint32_t n = 4u; n <= CC_NFUNCS; n <<= 1) {
        cfg.nfuncs = n;
        const uint64_t ns = time_cfg(&cfg);
        const uint64_t rel = hot ? (ns * 100u) / hot : 100u;
        const uint32_t span = cc_code_span(cc_table, CC_NFUNCS, n);
        if (within && rel <= KNEE_PERCENT)
            *knee_span = span;
        else
            within = 0;
        *worst_x100 = rel;
        if (g_csv) {
            out("code,%u,%u,%u.%02u,%u\n", (unsigned)n, (unsigned)span, (unsigned)(ns / 100u),
                (unsigned)(ns % 100u), (unsigned)rel);
        } else {
            cc_fmt_kib(a, span);
            ratio(b, rel);
            out("  %5u  %9s  %7u.%02u  %7s\n", (unsigned)n, a, (unsigned)(ns / 100u),
                (unsigned)(ns % 100u), b);
        }
    }
}

/**
 * @brief The data footprint sweep.
 * @param[in] base_chase Time of a load in the reference region, from hot_chase().
 * @param[in] base_call Time of a call in the hot reference, from hot_call().
 * @param[out] knee_bytes Largest size within KNEE_PERCENT of the reference.
 * @param[out] worst_x100 Slowdown of the chase at the largest size, times 100.
 * @param[out] max_ns100 Time of a load at the largest size.
 */
static void sweep_data(uint64_t base_chase, uint64_t base_call, uint32_t *knee_bytes,
    uint64_t *worst_x100, uint64_t *max_ns100)
{
    cc_cfg cfg = { HOT_FUNCS, 1u, 0u, HOT_DATA };
    int within = 1;
    char a[16], b[16];

    if (!g_csv)
        out("\ndata footprint (%u callbacks)\n   data KiB   chase ns/load   vs hot   ns/call   vs "
            "hot\n",
            (unsigned)HOT_FUNCS);
    *knee_bytes = 0;
    for (uint32_t bytes = 1024u; bytes <= CC_DATA_BYTES; bytes <<= 1) {
        const uint64_t chase = ns100_of_load(cc_chase_ticks_per_load_x1000(bytes));
        cfg.data_bytes = bytes;
        const uint64_t call = time_cfg(&cfg);
        const uint64_t rc = base_chase ? (chase * 100u) / base_chase : 100u;
        const uint64_t rk = base_call ? (call * 100u) / base_call : 100u;
        if (within && rc <= KNEE_PERCENT)
            *knee_bytes = bytes;
        else
            within = 0;
        *worst_x100 = rc;
        *max_ns100 = chase;
        if (g_csv) {
            out("data,%u,%u.%02u,%u,%u.%02u,%u\n", (unsigned)bytes, (unsigned)(chase / 100u),
                (unsigned)(chase % 100u), (unsigned)rc, (unsigned)(call / 100u),
                (unsigned)(call % 100u), (unsigned)rk);
        } else {
            cc_fmt_kib(a, bytes);
            ratio(b, rc);
            char c[16];
            ratio(c, rk);
            out("  %9s  %9u.%02u  %7s  %7u.%02u  %7s\n", a, (unsigned)(chase / 100u),
                (unsigned)(chase % 100u), b, (unsigned)(call / 100u), (unsigned)(call % 100u), c);
        }
    }
}

/**
 * @brief The skew sweep: all callbacks, the choice more or less concentrated.
 * @param[in] hot_ns100 Time of a call in the hot reference.
 */
static void sweep_skew(uint64_t hot_ns100)
{
    static const uint32_t skews[] = { 1u, 2u, 3u, 4u, 8u };
    cc_cfg cfg = { CC_NFUNCS, 1u, 0u, HOT_DATA };
    char b[16];

    if (!g_csv)
        out("\nskew of the choice (%u callbacks)\n  skew     ns/call   vs hot\n",
            (unsigned)CC_NFUNCS);
    for (unsigned i = 0; i < sizeof skews / sizeof skews[0]; i++) {
        cfg.skew = skews[i];
        const uint64_t ns = time_cfg(&cfg);
        const uint64_t rel = hot_ns100 ? (ns * 100u) / hot_ns100 : 100u;
        if (g_csv) {
            out("skew,%u,%u.%02u,%u\n", (unsigned)skews[i], (unsigned)(ns / 100u),
                (unsigned)(ns % 100u), (unsigned)rel);
        } else {
            ratio(b, rel);
            out("  %4u  %7u.%02u  %7s\n", (unsigned)skews[i], (unsigned)(ns / 100u),
                (unsigned)(ns % 100u), b);
        }
    }
}

/**
 * @brief The predictability sweep: the same callbacks in a random order, or in
 *        a short sequence that repeats.
 */
static void sweep_pattern(void)
{
    cc_cfg cfg = { 64u, 1u, 0u, HOT_DATA };
    char b[16];

    const uint64_t random = time_cfg(&cfg);
    cfg.period = 32u;
    const uint64_t repeat = time_cfg(&cfg);
    ratio(b, repeat ? (random * 100u) / repeat : 100u);
    if (g_csv) {
        out("pattern,%u.%02u,%u.%02u\n", (unsigned)(random / 100u), (unsigned)(random % 100u),
            (unsigned)(repeat / 100u), (unsigned)(repeat % 100u));
    } else {
        out("\npredictability of the choice (64 callbacks)\n  random order   %u.%02u ns/call\n  "
            "repeats /32    %u.%02u ns/call   (random is %s)\n",
            (unsigned)(random / 100u), (unsigned)(random % 100u), (unsigned)(repeat / 100u),
            (unsigned)(repeat % 100u), b);
    }
}

int cc_main(const cc_options *opt)
{
    cc_port_init();
    g_csv = opt->csv;
    cc_set_target_ms(opt->quick ? CC_TARGET_MS / 4u + 1u : CC_TARGET_MS);

    const int bad = cc_validate();
    if (g_csv) {
        out("cachecliff,%s,%08x,%u,%u,%u\n", CC_VERSION, (unsigned)cc_bank_id, (unsigned)CC_NFUNCS,
            (unsigned)CC_DATA_BYTES, (unsigned)cc_port_ticks_per_sec());
        out("validate,%d\n", bad);
    } else {
        out("cachecliff %s  bank %08x (%u callbacks)  data %u KiB  timer %u Hz\n", CC_VERSION,
            (unsigned)cc_bank_id, (unsigned)CC_NFUNCS, (unsigned)(CC_DATA_BYTES / 1024u),
            (unsigned)cc_port_ticks_per_sec());
        out("validation: %s\n", bad ? "FAILED" : "ok");
    }
    if (bad || opt->validate_only)
        return bad ? 1 : 0;

    uint64_t code_worst, data_worst, chase_max;
    uint32_t code_knee, data_knee;

    warm_up();
    const uint64_t hot = hot_call();
    const uint64_t base_chase = hot_chase();

    sweep_code(hot, &code_knee, &code_worst);
    sweep_data(base_chase, hot, &data_knee, &data_worst, &chase_max);
    sweep_skew(hot);
    sweep_pattern();

    char a[16], b[16], c[16];
    cc_fmt_kib(a, code_knee);
    cc_fmt_kib(b, data_knee);
    ratio(c, code_worst);
    char d[16];
    ratio(d, data_worst);
    if (g_csv) {
        out("summary,%u,%u,%u,%u,%u\n", (unsigned)(hot / 100u), (unsigned)code_knee,
            (unsigned)code_worst, (unsigned)data_knee, (unsigned)data_worst);
    } else {
        out("\nsummary\n"
            "  hot reference         %u.%02u ns/call\n"
            "  code within +%u%%      up to %s KiB\n"
            "  code, all callbacks   %s the hot time\n"
            "  data within +%u%%      up to %s KiB (pointer chase)\n"
            "  data, largest region  %s the reference, %u.%02u ns/load\n",
            (unsigned)(hot / 100u), (unsigned)(hot % 100u), (unsigned)(KNEE_PERCENT - 100u), a, c,
            (unsigned)(KNEE_PERCENT - 100u), b, d, (unsigned)(chase_max / 100u),
            (unsigned)(chase_max % 100u));
    }
    return 0;
}
