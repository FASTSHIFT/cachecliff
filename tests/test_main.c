/**
 * @file test_main.c
 * @brief Runner of the unit tests: the registry, the failure handling and main().
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cc_test.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

/** @brief Most test cases the runner takes. */
#define MAX_CASES 64

/** @brief The registered cases. */
static cc_test_case g_cases[MAX_CASES];

/** @brief Number of registered cases. */
static int g_ncases;

/** @brief Where a failed check jumps to. */
static jmp_buf g_leave;

/** @brief Set when the running case has failed. */
static int g_failed;

void cc_test_register(const char *name, void (*fn)(void))
{
    if (g_ncases < MAX_CASES) {
        g_cases[g_ncases].name = name;
        g_cases[g_ncases].fn = fn;
        g_ncases++;
    }
}

void cc_test_fail(const char *file, int line, const char *expr)
{
    printf("         %s:%d: FAILED: %s\n", file, line, expr);
    g_failed = 1;
    longjmp(g_leave, 1);
}

void cc_test_fail_eq(const char *file, int line, const char *expr, uint64_t a, uint64_t b)
{
    printf("         %s:%d: FAILED: %s (%llu vs %llu)\n", file, line, expr, (unsigned long long)a,
        (unsigned long long)b);
    g_failed = 1;
    longjmp(g_leave, 1);
}

/**
 * @brief Runs the registered cases.
 * @param[in] argc Number of arguments.
 * @param[in] argv With one argument, only the cases whose name contains it.
 * @return 0 if all passed, 1 if not.
 */
int main(int argc, char **argv)
{
    /* volatile: they are changed between setjmp() and longjmp() */
    volatile int passed = 0, failed = 0;

    for (volatile int i = 0; i < g_ncases; i++) {
        if (argc > 1 && !strstr(g_cases[i].name, argv[1]))
            continue;
        g_failed = 0;
        if (setjmp(g_leave) == 0)
            g_cases[i].fn();
        if (g_failed) {
            printf("[ FAIL ] %s\n", g_cases[i].name);
            failed++;
        } else {
            printf("[ PASS ] %s\n", g_cases[i].name);
            passed++;
        }
    }
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
