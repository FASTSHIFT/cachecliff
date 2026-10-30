# cachecliff

[简体中文](README.md) | **English**

**cachecliff** finds where the cache and memory system of a CPU starts to hurt.

CoreMark and most small benchmarks fit in the caches of any modern core, so they measure the pipeline. A real
program has more code than the instruction cache and more data than the data cache, and runs slower than its
score promised. cachecliff calls generated callbacks from a table, picks them at random, and times it, while it
changes **one thing at a time**:

| Sweep | Changes | Shows |
|-------|---------|-------|
| code footprint | how many callbacks are picked from | the size of the instruction cache, the cost of a miss |
| data footprint | the size of the data region, by a pointer chase and by the callbacks | every level of data cache, and the memory behind it |
| skew | how concentrated the choice is | how much a hot subset recovers |
| predictability | random order, or a short sequence that repeats | the cost of a mispredicted indirect call |

One build sweeps all of it: the footprints are run-time parameters, because a cache only sees the code that is
**executed**, not the code that is linked. The program prints its own report, like CoreMark. It needs a C99
compiler, a timer and an output, nothing else.

> It is an independent tool and needs no trace hardware. The report can be read together with an ETM or PMU trace
> of the same run: cachecliff says *where* the cliff is, and the trace says *why*.

## Quick start

```sh
make                    # build ./cachecliff
make run                # the full sweeps on this machine
./cachecliff --quick    # a shorter run
./cachecliff --csv      # machine-readable
```

```
code footprint (uniform choice, 4 KiB of data)
  funcs   code KiB     ns/call   vs hot
      4        1.5       35.03    0.97x
     64       22.9       35.22    0.98x
   1024      373.8       39.21    1.09x
data footprint (16 callbacks)
   data KiB   chase ns/load   vs hot   ns/call   vs hot
        1.0          1.82    1.01x       21.96    1.52x
       32.0          1.87    1.03x       22.70    1.58x
       64.0          5.02    2.78x       25.42    1.77x
    16384.0         75.83   41.66x      179.95    8.27x
...
summary
  code within +25%      up to 373.8 KiB
  data within +25%      up to 32.0 KiB (pointer chase)
```

The "within +25%" lines are the sizes up to which a footprint costs less than a quarter more than the hot
reference: the knees of the code and data caches. (The numbers above are from an x86 desktop.)

## What the callbacks are

A **bank** of 1024 functions, `uint32_t f(uint32_t x, cc_ctx *c)`, each a straight-line mix of arithmetic, loads
and stores at data-dependent addresses, and a few branches and small loops. They differ from each other, so none
shares code. `tools/ccgen.py` writes them, and the result is committed under `bank/`, so a build does not need
Python.

The sizes of the functions are *balanced*: the first 4, 8, 16 ... functions all have the same average size.
Without that, a sweep over the first few functions would compare different amounts of work per call.

The generator also computes, in Python, what the functions must return. The program checks its callbacks against
those values at start (`validation: ok`), the way CoreMark checks its CRC: if a compiler or a core gets one
wrong, the numbers are not worth reading.

## Building

| Target | |
|--------|---|
| `make` | `./cachecliff` for this machine (POSIX clock) |
| `make test` | unit tests, tests with the real timer, generator tests |
| `make asan` | the unit tests under AddressSanitizer and UBSan |
| `make coverage` | the unit tests with a gate on the line coverage of `src/` |
| `make cross` | compile the core for Cortex-M0/M3/M4/M7 |
| `make doc` | Doxygen documentation; any warning is an error |
| `make format-check` | clang-format, black, pylint |
| `make check` | format, test, asan, doc |

Capacity, to fit a small target (`include/cachecliff.h`):

```sh
make CC_NFUNCS=256 CC_DATA_BYTES=262144
```

A bigger or different bank:

```sh
python3 tools/ccgen.py --out bank --funcs 4096 --seed 7
python3 tools/ccgen.py --mix ld1=40,st=20     # more memory traffic
python3 tools/ccgen.py --sizes 4,8,16,32      # smaller functions
```

## Porting

Compile `include/`, `src/` and `bank/` with a port that provides four functions (`cc_port_*`, see
`port/posix/cc_port.c`):

```c
void     cc_port_init(void);
uint32_t cc_port_ticks(void);          /* free-running, wraps at 2^32 */
uint32_t cc_port_ticks_per_sec(void);
void     cc_port_puts(const char *s);
```

`port/cortex-m/cc_port.c` is that port for Cortex-M3/M4/M7/M33: the timer is the
DWT cycle counter, so a tick is a CPU cycle and the rate is `SystemCoreClock`.
The board provides `void cc_board_putc(char)` and sets clocks and caches up
before calling `cc_main()`. Put the bank and the data region where the
experiment needs them with `CC_FN_ATTR` and `CC_DATA_ATTR`, for example the
callbacks in RAM and the data in external memory.

## Validation on a Cortex-M7

cachecliff was run on an STM32H743 (150 MHz, 16 KiB I-cache and 16 KiB
D-cache, code in Flash, data in AXI SRAM) and every point was captured at the
same time with the ETM trace of [cortrace](https://github.com/cortrace/cortrace),
which counts cycles without the program's help. The self-reported
cycles per call agree with the trace's within 0.3% at all 23 points of the
code, data and cache sweeps (callbacks and dispatcher, 1 s per point).

| Sweep | What cachecliff reports | What the trace adds |
|---|---|---|
| code footprint | 151 cycles/call with 1.8 KiB of callbacks, 311 with 459 KiB. The I-cache knee is at 14 KiB | the extra cycles are fetch stalls **and** loads: the callbacks load their constants from literal pools in Flash through the D-cache (LSU 1.8 to 69 cycles/call) |
| same, `-mslow-flash-data` | 152 to 379 cycles/call | constants come from `movw`/`movt`, the LSU share stays 0, the whole growth is fetch stalls |
| data footprint | 164 cycles/call up to 8 KiB, 221 at 256 KiB. Dependent load: 3 cycles up to 16 KiB, 10.3 at 32 KiB, 12.0 in AXI SRAM | the growth is LSU only (1.8 to 60 cycles/call); issue and fetch stay put |
| caches (16 callbacks) | both on 164, I-cache off 345, D-cache off 416, both off 644 | I-cache off adds fetch stalls, D-cache off adds LSU |

The code footprint therefore measures the instruction cache and the data cache
together on a core that loads constants from the code section. That is how
compiled code behaves, and `-mslow-flash-data` separates the two.

## Development

```sh
scripts/install-hooks.sh    # clang-format, black, pylint, Conventional Commits
```

CI (`.github/workflows/ci.yml`) runs the format checks, builds with gcc and clang at `-Werror` (and at `-O3`),
the sanitizers, the coverage gate (95% of the lines of `src/`), the ARM cross builds, the documentation, and
checks that `bank/` is what `tools/ccgen.py` writes, and that a bank of another size also validates.

The unit tests use a timer that is made up, to check what the report does with the numbers. A made-up timer
cannot show that the measured code is really executed, which an optimizer can quietly stop doing, so
`tests/test_hosted.c` repeats the checks with the real clock: a measurement has to respond to what it measures.

## License

MIT, see `LICENSE`. © 2026 VIFEX
