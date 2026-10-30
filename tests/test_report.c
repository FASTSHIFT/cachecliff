/**
 * @file test_report.c
 * @brief Tests of the report that cc_main() prints, with the fake timer.
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cachecliff.h"
#include "cc_test.h"
#include "port_fake.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief Counts the occurrences of a string in the output.
 * @param[in] needle What to look for.
 * @return How many times it occurs.
 */
static int count(const char *needle)
{
    int n = 0;
    const char *p = fake_output();
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

TEST(report_has_all_the_sweeps_and_a_summary)
{
    const cc_options opt = { 0, 1, 0 };
    fake_set_step(20000u);
    fake_clear();
    CHECK_EQ(cc_main(&opt), 0);
    CHECK(strstr(fake_output(), "validation: ok") != NULL);
    CHECK(strstr(fake_output(), "code footprint") != NULL);
    CHECK(strstr(fake_output(), "data footprint") != NULL);
    CHECK(strstr(fake_output(), "skew of the choice") != NULL);
    CHECK(strstr(fake_output(), "predictability of the choice") != NULL);
    CHECK(strstr(fake_output(), "summary") != NULL);
    /* with the same time for every point nothing is slower than the hot reference */
    CHECK(strstr(fake_output(), "code, all callbacks   1.00x") != NULL);
    CHECK(strstr(fake_output(), "within +25%") != NULL);
}

TEST(report_prints_the_code_span_of_every_point)
{
    const cc_options opt = { 0, 1, 0 };
    fake_clear();
    CHECK_EQ(cc_main(&opt), 0);
    /* an unknown span is printed as '?'; the layout of this build is known */
    const char *code = strstr(fake_output(), "code footprint");
    const char *data = strstr(fake_output(), "data footprint");
    CHECK(code != NULL && data != NULL);
    for (const char *p = code; p < data; p++)
        CHECK(*p != '?');
}

TEST(csv_has_one_line_per_point)
{
    const cc_options opt = { 1, 1, 0 };
    fake_clear();
    CHECK_EQ(cc_main(&opt), 0);
    CHECK_EQ(count("\ncode,"), 9); /* 4 .. 1024 callbacks */
    CHECK_EQ(count("\ndata,"), 9); /* 1 KiB .. 256 KiB */
    CHECK_EQ(count("\nskew,"), 5);
    CHECK_EQ(count("\npattern,"), 1);
    CHECK_EQ(count("\nsummary,"), 1);
    CHECK(strstr(fake_output(), "validate,0") != NULL);
}

TEST(validate_only_prints_no_sweeps)
{
    const cc_options opt = { 0, 0, 1 };
    fake_clear();
    CHECK_EQ(cc_main(&opt), 0);
    CHECK(strstr(fake_output(), "validation: ok") != NULL);
    CHECK(strstr(fake_output(), "code footprint") == NULL);
}

TEST(report_marks_where_the_hot_time_is_exceeded)
{
    unsigned hot = 0, code_knee = 0, code_worst = 0, data_knee = 0, data_worst = 0;
    const cc_options opt = { 1, 1, 0 };

    /* every reading of the timer is a little slower than the one before */
    fake_set_step(20000u);
    fake_set_growth(500u);
    fake_clear();
    CHECK_EQ(cc_main(&opt), 0);
    fake_set_growth(0u);
    fake_set_step(20000u);

    const char *line = strstr(fake_output(), "\nsummary,");
    CHECK(line != NULL);
    CHECK_EQ(sscanf(line, "\nsummary,%u,%u,%u,%u,%u", &hot, &code_knee, &code_worst, &data_knee,
                 &data_worst),
        5);
    CHECK(code_worst > 125u); /* all callbacks are slower than +25% */
    CHECK(data_worst > 125u);
    CHECK(code_knee < 400u * 1024u); /* so the knee is below the largest footprint */
    CHECK(data_knee < CC_DATA_BYTES);
}

TEST(report_in_text_says_where_the_limit_is)
{
    const cc_options opt = { 0, 1, 0 };
    fake_set_step(20000u);
    fake_set_growth(500u);
    fake_clear();
    CHECK_EQ(cc_main(&opt), 0);
    fake_set_growth(0u);
    fake_set_step(20000u);
    CHECK(strstr(fake_output(), "code, all callbacks   1.00x") == NULL);
    CHECK(strstr(fake_output(), "within +25%      up to") != NULL);
}
