/**
 * @file cc_soak.h
 * @brief A loop that runs one configuration for as long as it is left alone,
 *        for looking at it with a trace, a profiler or a PMU.
 *
 * cc_main() sweeps and is over in seconds. To see where the time of one point
 * goes, the point has to run for as long as the other tool needs, and be
 * changeable without a reflash. cc_soak() does that: it reads g_cc_soak before
 * every batch of calls, so a debugger changes the configuration by writing the
 * words of the structure, and it times every batch itself, so that its own
 * numbers can be put next to the other tool's.
 *
 * The board must define CC_CMSIS_HEADER as the include file that declares
 * SCB_EnableICache() and its siblings, e.g. in a header that is force-included
 * (<tt>-include cc_config.h</tt>) so that the engine, the bank and this file
 * agree on it.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef CC_SOAK_H
#define CC_SOAK_H
#include <stdint.h>
/** @brief Bit of cc_soak_ctl::caches for the instruction cache. */
#define CC_SOAK_ICACHE 1u
/** @brief Bit of cc_soak_ctl::caches for the data cache. */
#define CC_SOAK_DCACHE 2u
/** @brief Value of cc_soak_ctl::mode: call the callbacks. */
#define CC_SOAK_CALLS 0u
/** @brief Value of cc_soak_ctl::mode: follow pointers through the data region. */
#define CC_SOAK_CHASE 1u
/**
 * @brief What the soak loop runs; written by the debugger, read before every batch.
 *
 * The fields are consecutive 32-bit words in this order.
 */
typedef struct cc_soak_ctl {
    uint32_t nfuncs; /**< Callbacks to pick from, a power of two up to CC_NFUNCS. */
    uint32_t skew; /**< See cc_cfg::skew. */
    uint32_t period; /**< See cc_cfg::period. */
    uint32_t data_bytes; /**< Part of the data region in use, a power of two. */
    uint32_t batch; /**< Calls per batch. */
    uint32_t caches; /**< CC_SOAK_ICACHE and CC_SOAK_DCACHE bits; applied between batches. */
    uint32_t mode; /**< CC_SOAK_CALLS or CC_SOAK_CHASE. */
} cc_soak_ctl;
/** @brief What the soak loop measured of itself; read by the debugger. */
typedef struct cc_soak_stat {
    uint32_t batches; /**< Batches since the last change of the configuration. */
    uint32_t calls_lo; /**< Calls since the last change (low word). */
    uint32_t ticks_lo; /**< Their time in port ticks (low word). */
    uint32_t last_ticks; /**< Time of the last batch. */
    uint32_t last_calls; /**< Calls of the last batch. */
    uint32_t sink; /**< The value the callbacks computed; it keeps them alive. */
    uint32_t chase_x1000; /**< CC_SOAK_CHASE: ticks per load, times 1000, of the last run. */
} cc_soak_stat;
/** @brief The configuration. */
extern volatile cc_soak_ctl g_cc_soak;
/** @brief The totals. */
extern volatile cc_soak_stat g_cc_stat;
/**
 * @brief Runs the soak loop; does not return.
 *
 * Prints a status line every CC_SOAK_STATUS_BATCHES batches.
 *
 * @param[in] caches The caches the board has turned on (CC_SOAK_ICACHE,
 *            CC_SOAK_DCACHE); it becomes the initial g_cc_soak.caches.
 */
void cc_soak(uint32_t caches) __attribute__((noreturn));
#endif /* CC_SOAK_H */
