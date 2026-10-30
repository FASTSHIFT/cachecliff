/**
 * @file test_engine.c
 * @brief Tests of the dispatcher, the timing, the pointer chase and the bank.
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cachecliff.h"
#include "cc_test.h"
#include "port_fake.h"

/** @brief Words of the region the tests use. */
#define WORDS 4096u

TEST(xorshift_known_values)
{
    uint32_t s = 1u;
    s = cc_xorshift(s);
    CHECK_EQ(s, 0x00042021u);
    s = cc_xorshift(s);
    CHECK_EQ(s, 0x04080601u);
    s = cc_xorshift(s);
    CHECK_EQ(s, 0x9dcca8c5u);
}

TEST(draw_matches_the_generators_python_implementation)
{
    uint32_t state = 1u;
    CHECK_EQ(cc_draw(&state, 64u, 1u), 0u);
    CHECK_EQ(state, 270369u);
    state = 1u;
    CHECK_EQ(cc_draw(&state, 64u, 2u), 0u);
    CHECK_EQ(state, 67634689u);
}

TEST(draw_stays_in_range_and_skew_concentrates_the_choice)
{
    uint32_t state = 7u, sum1 = 0, sum4 = 0, state0 = 7u;
    for (int i = 0; i < 20000; i++) {
        const uint32_t a = cc_draw(&state, 100u, 1u);
        CHECK(a < 100u);
        sum1 += a;
    }
    state = 7u;
    for (int i = 0; i < 20000; i++) {
        const uint32_t b = cc_draw(&state, 100u, 4u);
        CHECK(b < 100u);
        sum4 += b;
    }
    CHECK(sum1 / 20000u > 45u && sum1 / 20000u < 54u); /* about 49.5 */
    CHECK(sum4 / 20000u < 25u); /* about 19.8 */
    /* skew 0 is taken as 1 */
    state = 7u;
    const uint32_t first1 = cc_draw(&state, 100u, 1u);
    CHECK_EQ(cc_draw(&state0, 100u, 0u), first1);
}

TEST(data_fill_is_deterministic)
{
    uint32_t a[64], b[64];
    cc_data_fill(a, 64u, 5u);
    cc_data_fill(b, 64u, 5u);
    for (int i = 0; i < 64; i++)
        CHECK_EQ(a[i], b[i]);
    cc_data_fill(b, 64u, 6u);
    CHECK(a[0] != b[0]);
    /* the seed is made odd: 0 and 1 are the same seed */
    cc_data_fill(a, 64u, 0u);
    cc_data_fill(b, 64u, 1u);
    CHECK_EQ(a[10], b[10]);
}

TEST(the_data_region_is_the_configured_size_and_aligned)
{
    uint32_t *d = cc_data();
    CHECK(d != NULL);
    CHECK_EQ((uintptr_t)d % 64u, 0u);
    d[CC_DATA_BYTES / 4u - 1u] = 0xdeadbeefu; /* the last word is writable */
    CHECK_EQ(d[CC_DATA_BYTES / 4u - 1u], 0xdeadbeefu);
}

TEST(run_is_reproducible_and_the_seed_matters)
{
    const cc_cfg cfg = { 64u, 1u, 0u, WORDS * 4u };
    cc_data_fill(cc_data(), WORDS, 1u);
    const uint32_t a = cc_run(&cfg, 500u, 9u, 1u);
    cc_data_fill(cc_data(), WORDS, 1u);
    const uint32_t b = cc_run(&cfg, 500u, 9u, 1u);
    cc_data_fill(cc_data(), WORDS, 1u);
    const uint32_t c = cc_run(&cfg, 500u, 10u, 1u);
    CHECK_EQ(a, b);
    CHECK(a != c);
}

TEST(a_period_makes_the_sequence_repeat)
{
    const cc_cfg cfg = { 64u, 1u, 16u, WORDS * 4u };
    cc_data_fill(cc_data(), WORDS, 1u);
    const uint32_t two = cc_run(&cfg, 32u, 3u, 1u);
    cc_data_fill(cc_data(), WORDS, 1u);
    const uint32_t first = cc_run(&cfg, 16u, 3u, 1u);
    const uint32_t second = cc_run(&cfg, 16u, 3u, first);
    CHECK_EQ(two, second);
}

TEST(skew_changes_which_callbacks_run)
{
    cc_cfg cfg = { 256u, 1u, 0u, WORDS * 4u };
    cc_data_fill(cc_data(), WORDS, 1u);
    const uint32_t uniform = cc_run(&cfg, 300u, 2u, 1u);
    cfg.skew = 4u;
    cc_data_fill(cc_data(), WORDS, 1u);
    CHECK(cc_run(&cfg, 300u, 2u, 1u) != uniform);
}

TEST(the_callbacks_pass_the_validation_against_the_generator)
{
    CHECK_EQ(cc_validate(), 0);
    CHECK(cc_bank_nfuncs >= CC_NFUNCS);
    CHECK(cc_bank_id != 0u);
}

TEST(callbacks_are_in_the_table_in_address_order)
{
    for (uint32_t i = 1; i < CC_NFUNCS; i++)
        CHECK((uintptr_t)cc_table[i] > (uintptr_t)cc_table[i - 1]);
}

TEST(measure_reports_the_median_run_of_the_timer)
{
    const cc_cfg cfg = { 16u, 1u, 0u, 4096u };
    fake_set_step(20000u); /* exactly CC_TARGET_MS at 1 MHz */
    cc_set_target_ms(CC_TARGET_MS);
    const cc_result r = cc_measure(&cfg);
    CHECK_EQ(r.ticks, 20000u);
    CHECK_EQ(r.calls, 256u);
}

TEST(measure_runs_longer_when_the_timer_says_the_run_was_short)
{
    const cc_cfg cfg = { 16u, 1u, 0u, 4096u };
    fake_set_step(3000u); /* 3 ms: the run is made about 6 times longer */
    cc_set_target_ms(CC_TARGET_MS);
    const cc_result r = cc_measure(&cfg);
    CHECK_EQ(r.ticks, 3000u);
    CHECK(r.calls > 2000u);
    fake_set_step(20000u);
}

TEST(chase_builds_one_cycle_through_all_nodes)
{
    fake_set_step(20000u);
    cc_set_target_ms(CC_TARGET_MS);
    const uint32_t x1000 = cc_chase_ticks_per_load_x1000(4096u);
    CHECK_EQ(x1000, 20000u * 1000u / 1024u);

    /* 4096 bytes are 64 nodes; the walk from node 0 returns after exactly 64 steps */
    const uint32_t *d = cc_data();
    uint32_t p = d[0], steps = 1u;
    while (p != 0u && steps <= 64u) {
        p = d[p << 4];
        steps++;
    }
    CHECK_EQ(steps, 64u);
}

TEST(chase_of_a_tiny_region_still_works)
{
    fake_set_step(20000u);
    CHECK(cc_chase_ticks_per_load_x1000(64u) > 0u); /* one node is made two */
}

TEST(median3_sorts_in_every_order)
{
    static const uint32_t in[6][3]
        = { { 1, 2, 3 }, { 1, 3, 2 }, { 2, 1, 3 }, { 2, 3, 1 }, { 3, 1, 2 }, { 3, 2, 1 } };
    for (int i = 0; i < 6; i++) {
        uint32_t a[3] = { in[i][0], in[i][1], in[i][2] };
        cc_median3(a);
        CHECK_EQ(a[0], 1u);
        CHECK_EQ(a[1], 2u);
        CHECK_EQ(a[2], 3u);
    }
    uint32_t same[3] = { 5, 5, 5 };
    cc_median3(same);
    CHECK_EQ(same[1], 5u);
}

TEST(scale_calls_scales_and_clamps)
{
    CHECK_EQ(cc_scale_calls(1000u, 100u, 400u), 4000u); /* the run was 4 times too short */
    CHECK_EQ(cc_scale_calls(1000u, 400u, 100u), 256u); /* never fewer than 256 calls */
    CHECK_EQ(cc_scale_calls(1u << 20, 1u, 1u << 30), 1u << 28); /* nor more than 2^28 */
    CHECK_EQ(cc_scale_calls(1000u, 0u, 100u), 8000u); /* too short to see: eight times more */
}

TEST(validate_counts_every_wrong_value)
{
    uint32_t wrong[5];
    for (int i = 0; i < 5; i++)
        wrong[i] = cc_validate_all[i];
    CHECK_EQ(cc_validate_with(cc_table, CC_NFUNCS, cc_bank_nfuncs, wrong, cc_validate_draw), 0);
    wrong[0] ^= 1u;
    wrong[3] ^= 1u;
    CHECK_EQ(cc_validate_with(cc_table, CC_NFUNCS, cc_bank_nfuncs, wrong, cc_validate_draw), 2);
    CHECK_EQ(cc_validate_with(cc_table, CC_NFUNCS, cc_bank_nfuncs, cc_validate_all, 0u), 1);
}

TEST(validate_refuses_a_table_bigger_than_the_bank)
{
    CHECK_EQ(
        cc_validate_with(cc_table, CC_NFUNCS, CC_NFUNCS / 2u, cc_validate_all, cc_validate_draw),
        1000);
}

/** @brief Two callbacks for the span tests; only their addresses matter. */
static uint32_t cb_a(uint32_t x, cc_ctx *c)
{
    (void)c;
    return x + 1u;
}

/** @brief The second callback. */
static uint32_t cb_b(uint32_t x, cc_ctx *c)
{
    (void)c;
    return x + 2u;
}

TEST(code_span_follows_the_addresses_and_gives_up_on_a_reordered_layout)
{
    /* in the order of their addresses: the span is the distance to the next callback */
    const cc_fn ascending[3] = { cb_a, cb_b, cb_b };
    const cc_fn descending[3] = { cb_b, cb_a, cb_a };
    const uintptr_t a = (uintptr_t)cb_a & ~(uintptr_t)1u, b = (uintptr_t)cb_b & ~(uintptr_t)1u;
    const cc_fn *table = a < b ? ascending : descending;
    const cc_fn *wrong = a < b ? descending : ascending;
    const uintptr_t dist = a < b ? b - a : a - b;

    CHECK_EQ(cc_code_span(table, 3u, 1u), dist);
    CHECK_EQ(cc_code_span(wrong, 3u, 1u), 0u); /* the next one is below the first */
    /* the whole table: extrapolated from the first n-1 entries */
    CHECK_EQ(cc_code_span(table, 2u, 2u), dist * 2u);
    CHECK_EQ(cc_code_span(wrong, 2u, 2u), 0u);
}

TEST(fmt_kib_prints_one_decimal_and_a_question_mark_for_an_unknown_size)
{
    char buf[16];
    cc_fmt_kib(buf, 0u);
    CHECK(buf[0] == '?' && buf[1] == '\0');
    cc_fmt_kib(buf, 1536u);
    CHECK(buf[0] == '1' && buf[1] == '.' && buf[2] == '5' && buf[3] == '\0');
    cc_fmt_kib(buf, 4096u);
    CHECK(buf[0] == '4' && buf[1] == '.' && buf[2] == '0');
}

TEST(a_timer_that_does_not_advance_ends_the_pilot_at_the_cap)
{
    const cc_cfg cfg = { 16u, 1u, 0u, 4096u };
    fake_set_step(0u); /* every reading is the same: the run seems to take no time */
    cc_set_target_ms(CC_TARGET_MS);
    const cc_result r = cc_measure(&cfg);
    CHECK_EQ(r.ticks, 0u);
    CHECK(r.calls >= CC_PILOT_MAX_CALLS); /* it gave up looking for a run the timer can see */
    CHECK_EQ(cc_chase_ticks_per_load_x1000(4096u), 0u);
    fake_set_step(20000u);
}
