# cachecliff — 移植

## 1. 需要提供什么

编译 `include/`、`src/`、`bank/`，再加一个提供四个函数的 port（`cc_port_*`，范例见 `port/posix/cc_port.c`）：

```c
void     cc_port_init(void);
uint32_t cc_port_ticks(void);          /* 自由运行，在 2^32 回绕 */
uint32_t cc_port_ticks_per_sec(void);
void     cc_port_puts(const char *s);
```

再写一个 `main`，调用 `cc_main(&opt)`（`cc_options`：`csv`、`quick`、`validate_only`），返回值非零表示校验失败。

要求：C99 编译器、一个计时器、一个输出，别无其他。`cc_main` 用 `snprintf`，newlib-nano 够用，不需要浮点 printf。

## 2. 把库和数据放到想测的存储里

- `CC_FN_ATTR`：每个回调的属性，默认 `noinline`。要把回调放进 RAM/ITCM 就覆盖它，例如
  `__attribute__((section(".ramfunc"), noinline))`。
- `CC_DATA_ATTR`：数据区的属性，默认 64 字节对齐。例如 `__attribute__((section(".axi_bss"), aligned(64)))`。

这两个宏在引擎和库的每个文件里必须一致，所以放进一个头文件，用 `-include` 强制包含。

注意函数地址：代码足迹一栏的字节数是由相邻回调的地址之差算出来的，所以链接时要保持回调按文件顺序排列
（`-ffunction-sections` 加默认链接脚本即可）。工具链重排了函数，这一栏就没有意义，耗时曲线仍然有效。

## 3. Cortex-M

`port/cortex-m/cc_port.c` 适用于有 DWT 周期计数器的内核（M3、M4、M7、M33 以上；M0/M0+ 没有）：

- 计时器是 `DWT_CYCCNT`，一个 tick 是一个 CPU 周期，频率取 CMSIS 的 `SystemCoreClock`；
- 板子提供 `void cc_board_putc(char c)`，port 会把 `\n` 展开成 `\r\n`；
- port 不配时钟和缓存，调用 `cc_main()` 之前由板子配好，因为被测的就是那个配置。

测的是周期，不是时间，所以不同主频的报告可以直接比。

## 4. 浸泡循环

`cc_main()` 扫完就结束。要让别的工具（ETM trace、PMU、示波器）看清**一个点**，就用 `port/cortex-m/cc_soak.c`：
一个配置跑多久都行，配置在 RAM 里的 `g_cc_soak`，每批读一次，所以调试器用 `mww` 改它就行，不用重新烧录。
`g_cc_soak` 是 7 个连续的 32 位字：

| 字段 | 含义 |
|---|---|
| `nfuncs` `skew` `period` `data_bytes` | 同 `cc_cfg` |
| `batch` | 每批调用次数 |
| `caches` | bit 0 I-cache，bit 1 D-cache；批与批之间由 `cc_soak` 开关（关 D-cache 会先 clean） |
| `mode` | 0 调回调，1 指针追逐 |

`cc_soak` 自己用 port 的计时器给每一批计时，累计在 `g_cc_stat`
（`batches calls_lo ticks_lo last_ticks last_calls sink chase_x1000`），用别的工具抓同一时段就能和这个自报数字对照，
见 `04-validation-stm32h743.md`。板子要定义 `CC_CMSIS_HEADER`（声明 `SCB_EnableICache()` 等的头文件，放在 `-include` 的配置头里），
并在配好时钟、缓存之后调用 `cc_soak(caches)`，`caches` 是板子已经打开的缓存。

## 5. 范例

两块板子的胶水代码都在板子工程里，cachecliff 本身不含任何 SDK：

| 板子 | 位置 | 构建 |
|---|---|---|
| STM32H743（150 MHz，16 KiB 缓存） | `stm32h743-etm-trace-firmware` 的 `Core/cachecliff_port/` | `make CACHECLIFF=1` |
| i.MX RT1176（996 MHz，32 KiB 缓存） | `lv_port_nxp_evkmimxrt1170` 的 `cachecliff_port/` | `make -C cachecliff_port` |

i.MX RT1176 有两种构建：`make -C cachecliff_port` 把代码放在 QSPI Flash 里原地执行；`make -C cachecliff_port RAM=1` 把代码放进 OCRAM1，
用 `make RAM=1 load`（`load_ram.py`，pyocd）装载并启动，不写 Flash。板子上的 Flash 如果被别的程序设成了 DDR 连续读模式，pyocd 的 Flash 算法会初始化失败，
这时用 RAM 版最省事。串口读不出时（RT1176 的虚拟串口在这块板上读到的是乱码），`read_log.py` 直接从目标内存的日志缓冲区 `g_cc_log` 读出同样的文本。

两者都在启动时从串口打印完整报告，再进入浸泡循环；`EXTRA_CFLAGS="-DCC_CSV=1 -DCC_QUICK=0"` 让报告输出 CSV 并跑完整扫描，
把串口日志交给 `tools/ccreport.py` 就得到分析文档（见 `01-user-guide.md`）。STM32H743 的配置：

| 项 | 内容 |
|---|---|
| 回调 | 1024 个，在 Flash，走 I-cache |
| 数据区 | 256 KiB，在 AXI SRAM 的 `.axi_bss` 段，走 D-cache（DTCM 不过缓存，会盖住效果） |
| 输出 | USART1，115200 |
| 计时 | DWT_CYCCNT，150 MHz |
