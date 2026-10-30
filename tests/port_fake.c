/**
 * @file port_fake.c
 * @brief See port_fake.h.
 * @copyright SPDX-License-Identifier: MIT
 */
#include "port_fake.h"

#include "cachecliff.h"

#include <string.h>

/** @brief Size of the output buffer. */
#define OUT_BYTES 65536u

/** @brief The timer's value. */
static uint32_t g_now;

/** @brief How far the timer advances on a reading: 20 ms at 1 MHz. */
static uint32_t g_step = 20000u;

/** @brief How much the step grows on a reading. */
static uint32_t g_growth;

/** @brief The output. */
static char g_out[OUT_BYTES];

/** @brief Length of the output. */
static size_t g_len;

void fake_set_step(uint32_t step) { g_step = step; }

void fake_set_growth(uint32_t growth) { g_growth = growth; }

const char *fake_output(void) { return g_out; }

void fake_clear(void)
{
    g_len = 0;
    g_out[0] = '\0';
}

void cc_port_init(void) { }

uint32_t cc_port_ticks(void)
{
    g_now += g_step;
    g_step += g_growth;
    return g_now;
}

uint32_t cc_port_ticks_per_sec(void) { return 1000000u; }

void cc_port_puts(const char *s)
{
    const size_t n = strlen(s);
    if (g_len + n < OUT_BYTES) {
        memcpy(g_out + g_len, s, n + 1u);
        g_len += n;
    }
}
