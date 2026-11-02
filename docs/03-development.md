# cachecliff — 开发

## 1. 目录结构

| 路径 | 内容 |
|---|---|
| `include/cachecliff.h` | 全部公开接口、配置宏、Doxygen 文档 |
| `src/cc_engine.c` | 调度器、计时、指针追逐、校验 |
| `src/cc_main.c` | 扫描和报告 |
| `port/posix/` | 本机 port 和 `main` |
| `port/cortex-m/` | Cortex-M port（DWT 周期计数器）和浸泡循环 `cc_soak` |
| `bank/` | 生成的回调库（`cc_bank_000..015.c`、`cc_table.c`、`cc_bank.h`），**不要手改** |
| `tools/ccgen.py` | 库的生成器，只用 Python 标准库 |
| `tools/ccreport.py` | 把输出变成带 Mermaid 图的 Markdown 报告，只用 Python 标准库 |
| `tests/` | 单元测试（C）和生成器测试（Python） |
| `scripts/` | 格式、文档检查、安装 git hook |
| `docs/` | 这些文档；`docs/examples/` 是真实数据和 `ccreport.py` 的输出 |

## 2. 约定

- 所有 C 的函数、变量、宏都写 Doxygen 注释（`@brief`、`@param[in/out]`、`@return`）；生成的 C 也带 Doxygen。
  `Doxyfile` 里 `WARN_AS_ERROR = FAIL_ON_WARNINGS` 且要求每个参数都有文档，任何警告都算错误。
- 格式：C 用 clang-format（WebKit 风格，列宽 100，指针靠右），Python 用 black + pylint。`bank/` 是生成的，不格式化。
- 提交信息用英文 Conventional Commits：`type(scope): desc`，除 `docs` 外 scope 必填。

```sh
scripts/install-hooks.sh     # 装 pre-commit 和 commit-msg
```

pre-commit 检查 C 和 Python 的格式，并检查 `bank/` 与 `tools/ccgen.py` 的输出一致；commit-msg 检查提交信息格式。

## 3. 测试

| 目标 | 内容 |
|---|---|
| `make test` | 27 个单元测试（假计时器，检查引擎和报告怎么处理数字）、5 个真实计时器测试（`tests/test_hosted.c`）、8 个生成器测试（`tests/test_ccgen.py`）、13 个报告工具测试（`tests/test_ccreport.py`） |
| `make asan` | 单元测试在 AddressSanitizer + UBSan 下 |
| `make coverage` | 单元测试对 `src/` 的行覆盖率，门槛 95%（目前 100%） |
| `make cross` | M0/M3/M4/M7 编译核心，M3/M4/M7 编译 Cortex-M port |

为什么要有真实计时器测试：假计时器的单元测试能检查报告怎么处理数字，但看不出被测代码是不是真的在执行。
优化器会悄悄把没有副作用的循环删掉（指针追逐就发生过），那时假计时器照样"通过"。`test_hosted.c` 用真实时钟重复这些检查：
测量必须对被测量的东西有响应。

## 4. CI

`.github/workflows/ci.yml`：

| job | 内容 |
|---|---|
| format | clang-format、black、pylint |
| build + test（gcc、clang） | `-Werror` 构建、`-O3` 构建、全部测试 |
| sanitizers | ASan + UBSan |
| coverage | `src/` 行覆盖率 ≥ 95% |
| cross | Cortex-M0/M3/M4/M7 |
| docs | Doxygen，零警告，不依赖 Graphviz（`HAVE_DOT = NO`） |
| bank | `bank/` 与生成器输出一致，另一种大小的库也能通过校验 |

## 5. 文档

`make doc` 生成 HTML（`doc/html/`）。`README.md` 作为首页，`docs/*.md` 是其中的页面。
