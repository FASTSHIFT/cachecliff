/**
 * @file port_fake.h
 * @brief A port for the tests: a timer that advances by a fixed step on every
 *        reading, and an output that is kept in memory.
 *
 * With the fixed step every measurement takes the same time, so what the
 * engine and the report do with the numbers can be checked exactly.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef PORT_FAKE_H
#define PORT_FAKE_H

#include <stdint.h>

/**
 * @brief Sets how far the timer advances on every reading.
 * @param[in] step Ticks. The fake timer runs at 1 MHz.
 */
void fake_set_step(uint32_t step);

/**
 * @brief Makes the timer slower and slower: the step grows by this much on
 *        every reading.
 * @param[in] growth Ticks; 0 for a constant step.
 */
void fake_set_growth(uint32_t growth);

/**
 * @brief Everything printed since the last fake_clear().
 * @return A zero-terminated string.
 */
const char *fake_output(void);

/** @brief Forgets the output. */
void fake_clear(void);

#endif /* PORT_FAKE_H */
