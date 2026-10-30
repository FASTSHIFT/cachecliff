# cachecliff

**简体中文** | [English](README_en.md)

**cachecliff** 找出 CPU 的缓存和内存系统从哪里开始变慢。

CoreMark 和多数小型 benchmark 能整个装进任何现代内核的缓存，测到的只是流水线。真实程序的代码比指令缓存大，
数据比数据缓存大，跑出来比分数承诺的慢。cachecliff 从表里调用生成的回调函数，随机挑选并计时，每次**只改变一件事**：

| 扫描 | 改变什么 | 看到什么 |
|------|----------|----------|
| 代码足迹 | 从多少个回调里挑 | 指令缓存的大小，一次缺失的代价 |
| 数据足迹 | 数据区的大小，分别用指针追逐和回调访问 | 每一级数据缓存，以及后面的内存 |
| 倾斜度 | 挑选的集中程度 | 一个热点子集能挽回多少 |
| 可预测性 | 随机顺序，或一小段重复的序列 | 间接调用预测失败的代价 |

一次构建扫完全部：足迹是运行时参数，因为缓存只看见**被执行**的代码，而不是链接进来的代码。
程序像 CoreMark 一样自己打印报告。只需要 C99 编译器、一个计时器和一个输出，别无其他。

> 它是独立的工具，不依赖任何 trace 设备。报告可以和 ETM/PMU 的 trace 对照着读：cachecliff 说明悬崖*在哪里*，
> trace 说明*为什么*。

## 快速开始

```sh
make                    # 编译 ./cachecliff
make run                # 在本机跑完整扫描
./cachecliff --quick    # 更短的一轮
./cachecliff --csv      # 机器可读
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

“within +25%” 两行是足迹比热参考多花不到四分之一时所能到的最大尺寸，即代码缓存和数据缓存的拐点。
（上面的数字来自一台 x86 桌面机。）

## 回调是什么

一个有 1024 个函数的**库**，`uint32_t f(uint32_t x, cc_ctx *c)`，每个都是一段直线代码：算术、访问数据相关地址的读写，
加上少量分支和小循环。函数彼此不同，所以不共享代码。`tools/ccgen.py` 生成它们，结果提交在 `bank/` 下，
构建时不需要 Python。

函数的大小是**均衡**的：前 4、8、16 …… 个函数的平均大小相同。否则只扫前几个函数时，每次调用的工作量就不可比。

生成器同时用 Python 算出函数必须返回的值。程序启动时用这些值校验回调（`validation: ok`），
和 CoreMark 校验 CRC 一样：编译器或内核算错了，数字就不值得看。

## 构建

| 目标 | 作用 |
|------|------|
| `make` | 为本机编译 `./cachecliff`（POSIX 时钟） |
| `make test` | 单元测试、真实计时器测试、生成器测试 |
| `make asan` | 在 AddressSanitizer 和 UBSan 下跑单元测试 |
| `make coverage` | 跑单元测试，并对 `src/` 的行覆盖率设门槛 |
| `make cross` | 为 Cortex-M0/M3/M4/M7 编译核心 |
| `make doc` | 生成 Doxygen 文档，任何警告都算错误 |
| `make format-check` | clang-format、black、pylint |
| `make check` | 格式、测试、asan、文档 |

调整容量以适应小目标（`include/cachecliff.h`）：

```sh
make CC_NFUNCS=256 CC_DATA_BYTES=262144
```

换一个更大或不同的库：

```sh
python3 tools/ccgen.py --out bank --funcs 4096 --seed 7
python3 tools/ccgen.py --mix ld1=40,st=20     # 更多访存
python3 tools/ccgen.py --sizes 4,8,16,32      # 更小的函数
```

## 移植

把 `include/`、`src/`、`bank/` 和一个提供四个函数的 port 一起编译（`cc_port_*`，参见 `port/posix/cc_port.c`）：

```c
void     cc_port_init(void);
uint32_t cc_port_ticks(void);          /* 自由运行，在 2^32 回绕 */
uint32_t cc_port_ticks_per_sec(void);
void     cc_port_puts(const char *s);
```

`port/cortex-m/cc_port.c` 就是 Cortex-M3/M4/M7/M33 的 port：计时器是 DWT 周期计数器，一个 tick 是一个 CPU 周期，
频率取 `SystemCoreClock`。板子需要提供 `void cc_board_putc(char)`，并在调用 `cc_main()` 之前配好时钟和缓存。
用 `CC_FN_ATTR` 和 `CC_DATA_ATTR` 把库和数据区放到实验需要的位置，例如回调放 RAM、数据放外部存储器。

## 在 Cortex-M7 上的验证

cachecliff 在 STM32H743 上跑过（150 MHz，I-cache 和 D-cache 各 16 KiB，代码在 Flash，数据在 AXI SRAM），
每个点同时用 [cortrace](https://github.com/cortrace/cortrace) 的 ETM trace 抓取；trace 数周期，不依赖程序本身。
代码、数据、缓存三组扫描共 23 个点（回调加调度器，每点 1 秒），程序自报的每次调用周期数与 trace 的偏差都在 0.3% 以内。

| 扫描 | cachecliff 报告 | trace 补充的信息 |
|---|---|---|
| 代码足迹 | 回调 1.8 KiB 时 151 周期/次，459 KiB 时 311。I-cache 拐点在 14 KiB | 多出来的周期既有取指停顿，也有读：回调从 Flash 里的字面量池（literal pool）经 D-cache 读常数（LSU 从 1.8 涨到 69 周期/次） |
| 同上，`-mslow-flash-data` | 152 到 379 周期/次 | 常数改用 `movw`/`movt`，LSU 一直是 0，增长全是取指停顿 |
| 数据足迹 | 8 KiB 以内 164 周期/次，256 KiB 时 221。依赖读：16 KiB 以内 3 周期，32 KiB 为 10.3，AXI SRAM 为 12.0 | 增长只在 LSU（1.8 到 60 周期/次），发射和取指不变 |
| 缓存（16 个回调） | 全开 164，关 I-cache 345，关 D-cache 416，全关 644 | 关 I-cache 增加的是取指停顿，关 D-cache 增加的是 LSU |

所以在会从代码段里读常数的内核上，代码足迹扫描测到的是指令缓存和数据缓存的合计。编译出来的代码就是这样，
`-mslow-flash-data` 可以把两者分开。

## 开发

```sh
scripts/install-hooks.sh    # clang-format、black、pylint、Conventional Commits
```

CI 配置在 `.github/workflows/ci.yml`，跑这些检查：

- 格式：clang-format、black、pylint。
- gcc 和 clang 的 `-Werror` 构建，含 `-O3`。
- AddressSanitizer 和 UBSan。
- 覆盖率门槛：`src/` 的行覆盖率不低于 95%。
- Cortex-M0/M3/M4/M7 交叉编译。
- Doxygen 文档，零警告。
- `bank/` 就是 `tools/ccgen.py` 的输出，另一种大小的库也能通过校验。

单元测试用的是伪造的计时器，用来检查报告怎么处理数字。伪造的计时器无法证明被测代码真的在执行，
而优化器会悄悄地不再执行它，所以 `tests/test_hosted.c` 用真实时钟重复这些检查：测量必须对被测量的东西有响应。

## 许可证

MIT，见 `LICENSE`。 © 2026 VIFEX
