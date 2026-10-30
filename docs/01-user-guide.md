# cachecliff — 用户手册

## 1. 构建和运行

只需要 C99 编译器和 make（本机的 POSIX 构建）：

```sh
make                    # 编译 ./cachecliff
make run                # 完整扫描
./cachecliff --quick    # 每点的时间缩到 1/4
./cachecliff --csv      # 机器可读
./cachecliff --validate # 只校验回调
```

退出码：0 表示回调通过校验，1 表示没有（这时数字不要用）。

| 目标 | 作用 |
|------|------|
| `make` | 为本机编译 `./cachecliff`（POSIX 时钟） |
| `make test` | 单元测试、真实计时器测试、生成器测试 |
| `make asan` | 在 AddressSanitizer 和 UBSan 下跑单元测试 |
| `make coverage` | 跑单元测试，并对 `src/` 的行覆盖率设门槛 |
| `make cross` | 为 Cortex-M0/M3/M4/M7 编译核心，为 M3/M4/M7 编译 Cortex-M port |
| `make doc` | 生成 Doxygen 文档，任何警告都算错误 |
| `make format` / `make format-check` | clang-format、black、pylint（原地改 / 只检查） |
| `make check` | 格式、测试、asan、文档 |

编译选项：`OPT=-O2`（默认）、`CC=clang`、`WERROR=-Werror`。

## 2. 读报告

```
cachecliff 0.1.0  bank ebc8ae01 (1024 callbacks)  data 256 KiB  timer 150000000 Hz
validation: ok
code footprint (uniform choice, 4 KiB of data)
  funcs   code KiB     ns/call   vs hot
      4        1.8     1028.52    0.94x
     32       14.3     1125.23    1.03x
     64       28.5     1373.04    1.26x
   1024      459.4     2053.98    1.89x
data footprint (16 callbacks)
   data KiB   chase ns/load   vs hot   ns/call   vs hot
       16.0         20.00    1.00x     1159.94    1.06x
       32.0         68.52    3.42x     1324.50    1.22x
      256.0         80.01    4.00x     1462.26    1.34x
...
summary
  code within +25%      up to 14.3 KiB
  data within +25%      up to 16.0 KiB (pointer chase)
```

（上面是 STM32H743 的片段。）

| 节 | 怎么读 |
|---|---|
| 报头 | 版本、库的指纹、回调数、数据区大小、计时器频率 |
| `validation` | `ok` 才能用后面的数字 |
| code footprint | 只改回调数。`code KiB` 是这些回调占的代码字节数。`vs hot` 是相对 16 个回调的热参考 |
| data footprint | 只改数据区。`chase` 是依赖读的延迟，`ns/call` 是 16 个回调的调用耗时 |
| skew | 1024 个回调，选择越集中越快；看热点子集能挽回多少 |
| predictability | 64 个回调，随机顺序对比每 32 次重复的序列 |
| summary | 拐点：不超过热参考 125% 的最大足迹 |

`--csv` 的行：`code,<funcs>,<code字节>,<ns/call>,<相对热参考%>`；
`data,<字节>,<chase ns>,<%>,<ns/call>,<%>`；`skew,<k>,<ns/call>,<%>`；`pattern,<随机>,<重复>`；`summary,...`。

## 3. 容量

调整链接进来的容量，让程序装得进小目标（`include/cachecliff.h`）：

```sh
make CC_NFUNCS=256 CC_DATA_BYTES=262144
```

`CC_NFUNCS` 是 2 的幂，至少 64，不超过库的大小；`CC_DATA_BYTES` 是 2 的幂，至少 1024。
`CC_TARGET_MS`（每点至少多少毫秒）、`CC_FN_ATTR`（回调的属性）、`CC_DATA_ATTR`（数据区的属性）也能在命令行覆盖。
一个回调约 100 到 400 字节，1024 个约 370 到 460 KiB（Thumb-2，-O2）。

## 4. 换一个库

```sh
python3 tools/ccgen.py --out bank --funcs 4096 --seed 7
python3 tools/ccgen.py --mix ld1=40,st=20      # 更多访存
python3 tools/ccgen.py --sizes 4,8,16,32       # 更小的函数
```

`--funcs` 是 2 的幂且是 64 的倍数；`--sizes` 每 64 个函数里每个大小各占相同份数；`--mix` 的语句类见 `00-design.md`。
生成器只用 Python 标准库，相同参数得到相同文件。换库后要提交 `bank/`，CI 会检查 `bank/` 与生成器输出一致。
