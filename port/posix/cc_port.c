/**
 * @file cc_port.c
 * @brief Port for hosted systems with POSIX clocks (Linux, macOS, BSD).
 *
 * The timer counts nanoseconds, so a 32-bit tick wraps after 4.29 s; the
 * engine keeps each interval far below that.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
/** @brief Asks for clock_gettime() on strict C99 builds. */
#define _POSIX_C_SOURCE 199309L
#include "cachecliff.h"

#include <stdio.h>
#include <time.h>

void cc_port_init(void) { }

uint32_t cc_port_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec);
}

uint32_t cc_port_ticks_per_sec(void) { return 1000000000u; }

void cc_port_puts(const char *s)
{
    fputs(s, stdout);
    fflush(stdout);
}
