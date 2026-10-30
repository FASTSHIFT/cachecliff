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

一次构建扫完全部，程序像 CoreMark 一样自己打印报告，并用独立实现算出的值校验回调。只需要 C99 编译器、一个计时器和一个输出。
cachecliff 是独立的工具，不依赖任何 trace 设备；在 STM32H743 上已用 ETM 周期剖析交叉验证，自报与 trace 的偏差在 0.3% 以内。

```sh
make run                # 完整扫描（./cachecliff --quick 更短，--csv 机器可读）
```

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

（STM32H743，150 MHz。）

> 设计见 [`docs/00-design.md`](docs/00-design.md)，用法和读报告见 [`docs/01-user-guide.md`](docs/01-user-guide.md)，
> 移植见 [`docs/02-porting.md`](docs/02-porting.md)，开发和 CI 见 [`docs/03-development.md`](docs/03-development.md)，
> STM32H743 上的验证见 [`docs/04-validation-stm32h743.md`](docs/04-validation-stm32h743.md)。

## 许可证

MIT，见 `LICENSE`。 © 2026 VIFEX
