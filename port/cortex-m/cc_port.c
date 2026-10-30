/**
 * @file cc_port.c
 * @brief Port for Cortex-M cores that have the DWT cycle counter (M3, M4, M7,
 *        M33 and later).
 *
 * The timer is DWT_CYCCNT, so one tick is one CPU cycle and a measurement is
 * in cycles, exactly what a trace or a PMU counts. The rate is CMSIS's
 * SystemCoreClock, which the board must keep up to date.
 *
 * The board provides the output, one character at a time, as
 * cc_board_putc(). It does not set up clocks or caches: do that before
 * calling cc_main(), because that is the configuration being measured.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cachecliff.h"

/** @brief CMSIS: the CPU clock in Hz. */
extern uint32_t SystemCoreClock;

/**
 * @brief Prints one character; provided by the board.
 * @param[in] c The character.
 */
extern void cc_board_putc(char c);

/** @brief Debug Exception and Monitor Control Register. */
#define CC_DEMCR (*(volatile uint32_t *)0xE000EDFCu)
/** @brief DEMCR.TRCENA: turns on the DWT and the ITM. */
#define CC_DEMCR_TRCENA (1u << 24)
/** @brief DWT control register. */
#define CC_DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
/** @brief DWT_CTRL.CYCCNTENA: counts cycles. */
#define CC_DWT_CYCCNTENA (1u << 0)
/** @brief DWT cycle counter. */
#define CC_DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
/** @brief DWT lock access register (Cortex-M7 and M33). */
#define CC_DWT_LAR (*(volatile uint32_t *)0xE0001FB0u)

void cc_port_init(void)
{
    CC_DEMCR |= CC_DEMCR_TRCENA;
    CC_DWT_LAR = 0xC5ACCE55u;
    CC_DWT_CTRL |= CC_DWT_CYCCNTENA;
}

uint32_t cc_port_ticks(void) { return CC_DWT_CYCCNT; }

uint32_t cc_port_ticks_per_sec(void) { return SystemCoreClock; }

void cc_port_puts(const char *s)
{
    for (; *s != '\0'; s++) {
        if (*s == '\n')
            cc_board_putc('\r');
        cc_board_putc(*s);
    }
}
