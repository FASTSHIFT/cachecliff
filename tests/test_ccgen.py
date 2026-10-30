#!/usr/bin/env python3
"""Tests of tools/ccgen.py: it is deterministic, the committed bank is what it
generates, every group holds the same sizes, and the Python implementation of
the statements agrees with itself on a known case.

Run: python3 tests/test_ccgen.py
"""

import filecmp
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "tools", "ccgen.py")
BANK = os.path.join(ROOT, "bank")

sys.path.insert(0, os.path.join(ROOT, "tools"))
import ccgen  # noqa: E402  pylint: disable=wrong-import-position


def generate(out, *extra):
    subprocess.run(
        [sys.executable, GEN, "--out", out, *extra], check=True, capture_output=True
    )


def same_dirs(a, b):
    names = sorted(os.listdir(a))
    if names != sorted(os.listdir(b)):
        return False
    return all(
        filecmp.cmp(os.path.join(a, n), os.path.join(b, n), shallow=False)
        for n in names
    )


def test_deterministic_and_committed_bank_is_up_to_date():
    with tempfile.TemporaryDirectory() as t1, tempfile.TemporaryDirectory() as t2:
        generate(t1)
        generate(t2)
        assert same_dirs(t1, t2), "two runs differ"
        assert same_dirs(t1, BANK), "bank/ is not what tools/ccgen.py generates; run it"


def test_seed_and_mix_change_the_bank():
    with tempfile.TemporaryDirectory() as t1, tempfile.TemporaryDirectory() as t2:
        generate(t1, "--funcs", "128")
        generate(t2, "--funcs", "128", "--seed", "2")
        assert not same_dirs(t1, t2)
        generate(t2, "--funcs", "128", "--mix", "ld1=40,st=20")
        assert not same_dirs(t1, t2)


def test_every_group_holds_the_same_sizes():
    sizes_per_group = []
    for name in sorted(os.listdir(BANK)):
        if not re.fullmatch(r"cc_bank_\d+\.c", name):
            continue
        with open(os.path.join(BANK, name), encoding="utf-8") as f:
            sizes = sorted(int(m) for m in re.findall(r": (\d+) statements", f.read()))
        sizes_per_group.append(sizes)
    assert len(sizes_per_group) == 16
    assert all(len(s) == ccgen.GROUP for s in sizes_per_group)
    assert all(s == sizes_per_group[0] for s in sizes_per_group)


def test_every_power_of_two_prefix_has_the_mean_size():
    """A sweep over the first 4, 8, 16 ... functions compares the same work per call."""
    sizes = []
    for name in sorted(os.listdir(BANK)):
        if re.fullmatch(r"cc_bank_\d+\.c", name):
            with open(os.path.join(BANK, name), encoding="utf-8") as f:
                sizes += [int(m) for m in re.findall(r": (\d+) statements", f.read())]
    mean = sum(sizes) / len(sizes)
    for n in (4, 8, 16, 32, 64, 128, 256, 512, 1024):
        prefix = sum(sizes[:n]) / n
        assert (
            abs(prefix - mean) / mean < 0.12
        ), f"first {n}: {prefix:.1f} vs {mean:.1f}"


def test_balanced_order_keeps_the_sizes():
    rng = random.Random(3)
    for _ in range(200):
        order = ccgen.balanced_order(rng, [6, 8, 12, 16, 24, 32, 48, 64])
        assert sorted(order) == [6, 8, 12, 16, 24, 32, 48, 64]
        # the smallest point of a sweep is 4 functions: it must not be unlucky
        assert abs(sum(order[:4]) / 4 - 26.25) / 26.25 < 0.03


def test_functions_are_documented_and_declared_in_the_table():
    with open(os.path.join(BANK, "cc_bank_000.c"), encoding="utf-8") as f:
        text = f.read()
    assert len(re.findall(r"@brief Callback \d+ of the bank", text)) == ccgen.GROUP
    assert text.count("@return The new value.") == ccgen.GROUP
    with open(os.path.join(BANK, "cc_table.c"), encoding="utf-8") as f:
        table = f.read()
    assert "CC_FN_DECL(cc_f1023);" in table and "cc_validate_draw" in table


def test_statement_semantics_known_case():
    d = [0] * 8
    stmts = [("mul", 3), ("alu3", 5, 1), ("st", 0), ("ld1", 0)]
    x = 7
    for s in stmts:
        x = ccgen.run_statement(s, x, d, 7)
    # 7*3=21; (21+5)^(21>>1)=26^10=16; d[16&7=0]=16; x += d[(16^0)&7=0] -> 32
    assert x == 32 and d[0] == 16


def test_draw_matches_the_documented_formula():
    state = 1
    j, state2 = ccgen.draw(state, 64, 1)
    s = ccgen.xorshift(1)
    assert state2 == s and j == (s * 64) >> 32
    j2, _ = ccgen.draw(1, 64, 3)
    assert 0 <= j2 < 64


def main():
    """Runs the tests without pytest; returns the exit code."""
    failed = 0
    for test_name, fn in sorted(globals().items()):
        if test_name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"[ PASS ] {test_name}")
            except AssertionError as exc:
                failed += 1
                print(f"[ FAIL ] {test_name}: {exc}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
