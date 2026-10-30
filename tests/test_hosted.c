/**
 * @file test_hosted.c
 * @brief Tests with the real timer of the hosted port.
 *
 * The other tests use a timer that is made up, which cannot show that the
 * measured code is really executed: an optimizer that drops a loop gives the
 * same number every time. Here the timer is real and a measurement has to
 * respond to what it measures.
 *
 * Built by `make test-hosted` (not by `make test`): the results depend on the
 * machine, so the checks are wide.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#include "cachecliff.h"
#include "cc_test.h"

TEST(a_chase_takes_time_in_proportion_to_its_steps)
{
    uint32_t sink = 0;
    (void)cc_chase_ticks_per_load_x1000(4096u); /* builds the cycle */
    (void)cc_chase(1u << 16, &sink); /* warm up */
    const uint32_t few = cc_chase(1u << 18, &sink);
    const uint32_t many = cc_chase(1u << 22, &sink);
    CHECK(few > 0u);
    /* 16 times the steps: well over 4 times the time, whatever the machine */
    CHECK(many > few * 4u);
}

TEST(a_chase_through_a_big_region_is_slower_than_through_a_small_one)
{
    cc_set_target_ms(5u);
    const uint32_t small = cc_chase_ticks_per_load_x1000(4096u);
    const uint32_t big = cc_chase_ticks_per_load_x1000(CC_DATA_BYTES);
    CHECK(small > 0u);
    /* a load costs at least a fraction of a nanosecond, and a region that does
       not fit in the cache costs more than one that does */
    CHECK(small > 100u); /* 0.1 ns per load, with 1 tick = 1 ns and x1000 */
    CHECK(big > small * 2u);
}

TEST(a_run_takes_time_in_proportion_to_its_calls)
{
    const cc_cfg cfg = { 64u, 1u, 0u, 4096u };
    cc_data_fill(cc_data(), 1024u, 1u);
    const uint32_t t0 = cc_port_ticks();
    (void)cc_run(&cfg, 20000u, 1u, 1u);
    const uint32_t t1 = cc_port_ticks();
    (void)cc_run(&cfg, 320000u, 1u, 1u);
    const uint32_t t2 = cc_port_ticks();
    CHECK(t1 - t0 > 0u);
    CHECK((t2 - t1) > (t1 - t0) * 4u); /* 16 times the calls */
}

TEST(a_call_costs_more_than_a_nanosecond_and_less_than_a_millisecond)
{
    const cc_cfg cfg = { 16u, 1u, 0u, 4096u };
    cc_set_target_ms(5u);
    const cc_result r = cc_measure(&cfg);
    const uint32_t ns_per_call = r.ticks / r.calls; /* 1 tick = 1 ns on this port */
    CHECK(ns_per_call >= 1u);
    CHECK(ns_per_call < 1000000u);
    CHECK(r.calls >= 256u);
}

TEST(the_whole_report_runs_on_the_real_timer)
{
    const cc_options opt = { 1, 1, 0 };
    CHECK_EQ(cc_main(&opt), 0);
}
