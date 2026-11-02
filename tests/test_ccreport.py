#!/usr/bin/env python3
"""Tests of tools/ccreport.py: it reads the CSV of cachecliff, tolerates noise around
it, and writes a report with Mermaid charts.

Run: python3 tests/test_ccreport.py
"""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "ccreport.py")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ccreport  # noqa: E402  pylint: disable=wrong-import-position

# A small run with a cliff in the code footprint at 64 callbacks and two levels of data.
CSV = """cachecliff,0.1.0,ebc8ae01,1024,262144,150000000
validate,0
code,4,1536,100.00,95
code,8,3040,105.00,100
code,16,5968,106.00,101
code,32,11824,110.00,105
code,64,23520,180.00,170
code,128,47296,230.00,220
data,1024,20.00,100,1000.00,100
data,2048,20.00,100,1001.00,100
data,4096,20.00,100,1002.00,100
data,8192,20.00,100,1010.00,101
data,16384,21.00,105,1100.00,110
data,32768,70.00,350,1300.00,130
data,65536,80.00,400,1400.00,140
data,131072,80.00,400,1410.00,141
skew,1,200.00,100
skew,2,210.00,105
pattern,150.00,100.00
summary,105,11824,170,16384,400
"""


def run_tool(*args, stdin=None):
    return subprocess.run(
        [sys.executable, TOOL, *args],
        input=stdin,
        capture_output=True,
        text=True,
        check=False,
    )


def test_parse_reads_every_kind_of_line():
    run = ccreport.parse(CSV, "x")
    assert run.hz == 150_000_000 and run.nfuncs == 1024 and run.bank == "ebc8ae01"
    assert len(run.code) == 6 and len(run.data) == 8 and len(run.skew) == 2
    assert run.pattern == (150.0, 100.0) and run.hot_ns == 105.0 and run.bad == 0


def test_parse_ignores_noise_and_damaged_lines():
    noisy = (
        "\x00\xffboot banner\r\n"
        + CSV.replace("\n", "\r\n")
        + "soak funcs=1024\r\ncode,9\r\n"
    )
    run = ccreport.parse(noisy, "x")
    assert len(run.code) == 6, "the damaged line must be skipped, the others kept"
    # a byte of noise in front of a line on the same row
    run = ccreport.parse("junk" + CSV, "x")
    assert run.version == "0.1.0"


def test_parse_takes_the_last_report():
    run = ccreport.parse(CSV + CSV.replace("0.1.0", "0.2.0"), "x")
    assert run.version == "0.2.0" and len(run.code) == 6


def test_parse_rejects_text_that_is_not_the_csv():
    try:
        ccreport.parse("hello\nworld\n", "x")
    except ccreport.CsvError:
        return
    raise AssertionError("no CsvError")


def test_knee_and_levels():
    run = ccreport.parse(CSV, "x")
    within, beyond = ccreport.knee(run.code)
    assert within[0] == 32 and beyond[0] == 64
    lv = ccreport.levels([(r[0], r[1]) for r in run.data])
    assert len(lv) == 2, lv
    assert lv[0]["last"] == 16384 and lv[1]["x0"] == 32768


def test_biggest_step():
    assert ccreport.biggest_step([(1, 10.0), (2, 11.0), (3, 30.0), (4, 31.0)]) == (
        2,
        3,
        30 / 11,
    )
    assert ccreport.biggest_step([(1, 10.0)]) is None


def test_ceil_nice():
    assert ccreport.ceil_nice(0) == 1
    assert ccreport.ceil_nice(23.1) == 25
    assert ccreport.ceil_nice(100) == 100
    assert ccreport.ceil_nice(101) == 120


def test_report_has_charts_and_findings_in_both_languages():
    for lang, word in (("zh", "拐点"), ("en", "knee")):
        res = run_tool("--lang", lang, "-", stdin=CSV)
        assert res.returncode == 0, res.stderr
        assert res.stdout.count("```mermaid") >= 4
        assert "xychart-beta" in res.stdout and word in res.stdout
        assert "| 32 |" in res.stdout and "| 64 |" in res.stdout


def test_skew_wording_follows_the_sign():
    slower = ccreport.parse(CSV, "x")
    faster = ccreport.parse(CSV.replace("skew,2,210.00,105", "skew,2,150.00,75"), "x")
    t = ccreport.TEXT["en"]
    assert "not faster" in ccreport.other_findings(slower, t)[0]
    assert "75%" in ccreport.other_findings(faster, t)[0]


def test_the_plain_report_is_read_like_the_csv():
    text = """\
junk
cachecliff 0.1.0  bank ebc8ae01 (1024 callbacks)  data 256 KiB  timer 150000000 Hz
validation: ok
code footprint (uniform choice, 4 KiB of data)
  funcs   code KiB     ns/call   vs hot
      4        1.8     1028.52    0.94x
     64       28.5     1373.04    1.26x
data footprint (16 callbacks)
   data KiB   chase ns/load   vs hot   ns/call   vs hot
        1.0         20.00    1.00x     1087.10    1.00x
      256.0         80.01    4.00x     1462.26    1.34x
skew of the choice (1024 callbacks)
  skew     ns/call   vs hot
     1     2054.51    1.89x
     8     2359.78    2.17x
predictability of the choice (64 callbacks)
  random order   1373.24 ns/call
  repeats /32    1093.12 ns/call   (random is 1.25x)
"""
    run = ccreport.parse(text, "x")
    assert run.hz == 150_000_000 and run.data_bytes == 256 * 1024 and run.bad == 0
    assert run.code == [(4, 1843, 1028.52, 94), (64, 29184, 1373.04, 126)]
    assert run.data[1] == (262144, 80.01, 400, 1462.26, 134)
    assert run.skew[1] == (8, 2359.78, 217) and run.pattern == (1373.24, 1093.12)
    failed = ccreport.parse(text.replace("validation: ok", "validation: FAILED"), "x")
    assert failed.bad == 1


def test_several_files_share_the_charts():
    with tempfile.TemporaryDirectory() as d:
        a, b, out = (os.path.join(d, n) for n in ("on.csv", "off.csv", "r.md"))
        for path, text in ((a, CSV), (b, CSV.replace("180.00", "400.00"))):
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
        res = run_tool(a, b, "-o", out, "-l", "cache on")
        assert res.returncode == 0, res.stderr
        with open(out, encoding="utf-8") as f:
            doc = f.read()
        assert "cache on" in doc and "off" in doc
        assert doc.count("    line [") >= 8, "two lines per chart"


def test_cycles_turns_ns_into_ticks():
    res = run_tool("--cycles", "--lang", "en", "-", stdin=CSV)
    assert res.returncode == 0, res.stderr
    assert "cycles/call" in res.stdout and "ns/call" not in res.stdout
    # 100 ns at 150 MHz is 15 cycles: the first point of the code footprint
    assert "| 4 | 1.5 | 15.00 |" in res.stdout


def test_a_failed_validation_is_flagged_and_exits_1():
    res = run_tool("-", stdin=CSV.replace("validate,0", "validate,3"))
    assert res.returncode == 1
    assert "校验" in res.stdout and "不能用" in res.stdout


def test_bad_input_exits_2():
    res = run_tool("-", stdin="nothing here\n")
    assert res.returncode == 2 and "cachecliff" in res.stderr
    assert run_tool("/no/such/file.csv").returncode == 2


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
