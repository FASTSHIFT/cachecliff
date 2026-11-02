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

One build sweeps all of it. The program prints its own report, like CoreMark, and checks its callbacks against
values computed by an independent implementation. It needs a C99 compiler, a timer and an output, nothing else.
cachecliff is independent of any trace hardware; on an STM32H743 it was cross-validated against an ETM cycle
profile, and the self-reported numbers agree with the trace within 0.3%.

```sh
make run                # the full sweeps (./cachecliff --quick is shorter, --csv is machine-readable)
make report             # the same, and write report.md with charts and findings
```

A console log captured on a device works too: `python3 tools/ccreport.py uart.log -o report.md`
(example: [`docs/examples/h743.md`](docs/examples/h743.md)).

```
code footprint (uniform choice, 4 KiB of data)
  funcs   code KiB     ns/call   vs hot
      4        1.8     1028.52    0.94x
     32       14.3     1125.23    1.03x
     64       28.5     1373.04    1.26x
   1024      459.4     2053.98    1.89x
...
summary
  code within +25%      up to 14.3 KiB
  data within +25%      up to 16.0 KiB (pointer chase)
```

(STM32H743, 150 MHz.)

> Design: [`docs/00-design.md`](docs/00-design.md). Usage and reading the report: [`docs/01-user-guide.md`](docs/01-user-guide.md).
> Porting: [`docs/02-porting.md`](docs/02-porting.md). Development and CI: [`docs/03-development.md`](docs/03-development.md).
> Validation on an STM32H743: [`docs/04-validation-stm32h743.md`](docs/04-validation-stm32h743.md). The documents are in Chinese.

## License

MIT, see `LICENSE`. © 2026 VIFEX
