/**
 * @file cc_test.h
 * @brief A minimal unit-test framework for cachecliff: no dependencies, GCC or
 *        Clang.
 *
 * Define a case with TEST(), check with CHECK() and CHECK_EQ(). A case stops at
 * its first failed check. main() is in tests/test_main.c.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef CC_TEST_H
#define CC_TEST_H

#include <stdint.h>
#include <stdio.h>

/** @brief A registered test case. */
typedef struct cc_test_case {
    const char *name; /**< Name shown in the report. */
    void (*fn)(void); /**< The case. */
} cc_test_case;

/**
 * @brief Registers a case; called before main() by TEST().
 * @param[in] name Name of the case.
 * @param[in] fn The case.
 */
void cc_test_register(const char *name, void (*fn)(void));

/**
 * @brief Records a failed check and leaves the case.
 * @param[in] file Source file.
 * @param[in] line Line.
 * @param[in] expr What failed.
 */
void cc_test_fail(const char *file, int line, const char *expr);

/**
 * @brief Records a failed comparison and leaves the case.
 * @param[in] file Source file.
 * @param[in] line Line.
 * @param[in] expr What was compared.
 * @param[in] a The left value.
 * @param[in] b The right value.
 */
void cc_test_fail_eq(const char *file, int line, const char *expr, uint64_t a, uint64_t b);

/**
 * @brief Defines and registers a test case.
 * @param name Identifier of the case.
 */
#define TEST(name)                                                                                 \
    static void test_##name(void);                                                                 \
    __attribute__((constructor)) static void reg_##name(void)                                      \
    {                                                                                              \
        cc_test_register(#name, test_##name);                                                      \
    }                                                                                              \
    static void test_##name(void)

/**
 * @brief Fails the case unless the expression is true.
 * @param expr The expression.
 */
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr))                                                                               \
            cc_test_fail(__FILE__, __LINE__, #expr);                                               \
    } while (0)

/**
 * @brief Fails the case unless the two values are equal.
 * @param a The left value, an integer.
 * @param b The right value, an integer.
 */
#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        uint64_t va_ = (uint64_t)(a), vb_ = (uint64_t)(b);                                         \
        if (va_ != vb_)                                                                            \
            cc_test_fail_eq(__FILE__, __LINE__, #a " == " #b, va_, vb_);                           \
    } while (0)

#endif /* CC_TEST_H */
