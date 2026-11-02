/**
 * @file cmsis_stub.h
 * @brief Stands in for the vendor CMSIS header, so that the Cortex-M soak loop can be
 *        compile-checked without a vendor SDK.
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef CMSIS_STUB_H
#define CMSIS_STUB_H
/** @brief Does nothing. */
static inline void SCB_EnableICache(void) { }
/** @brief Does nothing. */
static inline void SCB_DisableICache(void) { }
/** @brief Does nothing. */
static inline void SCB_EnableDCache(void) { }
/** @brief Does nothing. */
static inline void SCB_DisableDCache(void) { }
#endif /* CMSIS_STUB_H */
