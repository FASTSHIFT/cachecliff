/**
 * @file main.c
 * @brief Command line of the hosted build.
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cachecliff.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief Entry point.
 * @param[in] argc Number of arguments.
 * @param[in] argv Arguments: <tt>--csv</tt>, <tt>--quick</tt>, <tt>--validate</tt>.
 * @return 0 if the callbacks passed the validation, 1 if not, 2 for a bad option.
 */
int main(int argc, char **argv)
{
    cc_options opt = { 0, 0, 0 };

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--csv"))
            opt.csv = 1;
        else if (!strcmp(argv[i], "--quick"))
            opt.quick = 1;
        else if (!strcmp(argv[i], "--validate"))
            opt.validate_only = 1;
        else {
            fprintf(stderr, "usage: %s [--csv] [--quick] [--validate]\n", argv[0]);
            return 2;
        }
    }
    return cc_main(&opt);
}
