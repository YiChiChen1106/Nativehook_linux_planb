# Linux 内存泄漏检测统一 Benchmark

这个目录提供一套可在 Linux 服务器上运行的统一 Benchmark，用于比较四种行业工具：

- Valgrind Memcheck
- LSan/ASan
- BCC memleak
- heaptrack

本目录只包含四工具实验。所有工具运行同一份 C++ 负载，并使用相同的线程数、迭代次数、分配大小和延迟参数。

## 目录结构

```text
benchmarks/leak_benchmark.cpp               六类统一负载
scripts/run_leak_benchmark_cases.sh        直接运行负载并核对理论值
scripts/run_leak_tool_compare.sh            逐工具、逐场景运行
scripts/run_leak_tool_formal_experiment.sh  重复实验与汇总
scripts/run_game_like_blind_experiment.sh   游戏语义盲测与三组参数 profile
tools/summarize_tool_compare.py             输出单轮 CSV/Markdown
tools/summarize_formal_experiment.py        输出多轮 CSV/Markdown
tests/                                      dry-run 检查脚本
```

## 构建

```bash
cmake -S . -B build
cmake --build build --target leak_benchmark -j
```

如果服务器没有 CMake，两个 runner 在需要构建时会回退到 `g++`。LSan 需要 `clang++`，BCC memleak 需要已安装 BCC、内核能力和免交互 `sudo` 权限，heaptrack 需要 `heaptrack` 与可选的 `heaptrack_print`。

## 六类负载

| Case | 目的 | 理论状态 |
| --- | --- | --- |
| `no_leak` | 平衡分配和释放，观察误报 | 无 outstanding allocation |
| `definite_leak` | 固定路径产生明确泄漏 | 结束时仍有未释放对象 |
| `growth_leak` | 每轮增加未释放对象 | outstanding 数量持续增长 |
| `delayed_free` | 对象延迟一段时间后释放 | 长生命周期对象，最终无泄漏 |
| `high_freq_no_leak` | 高频平衡路径 | 用于观察运行开销 |
| `mixed` | 平衡释放、延迟释放和明确泄漏混合出现 | 同时观察检测和输出可读性 |

每个 case 都在同一份 `leak_benchmark` 上执行。`definite_leak` 和 `growth_leak` 的未释放对象来自明确的 Benchmark 调用路径；`delayed_free` 通过延迟释放区分长生命周期与真正泄漏。

## 运行单轮对比

先确认工具和权限，再执行：

```bash
THREADS=2 \
ITERATIONS=2000 \
SIZE=64 \
ROUNDS=6 \
DELAY_MS=100 \
POST_DELAY_MS=1000 \
scripts/run_leak_tool_compare.sh
```

结果默认写入 `results/tool_compare/<timestamp>/`，目录下按工具和 case 保存原始日志、Benchmark JSON、运行配置和退出码。可以用 `--tool` 或 `--case` 缩小范围：

```bash
scripts/run_leak_tool_compare.sh --tool valgrind --case definite_leak
```

单轮完成后运行汇总器：

```bash
python3 tools/summarize_tool_compare.py results/tool_compare/<timestamp>
```

它会生成 `tool_compare_summary.csv` 和 `tool_compare_summary.md`。解析时优先匹配 Benchmark 的泄漏调用路径；运行库、线程库或工具自身的分配会记录到 `notes`，不会直接计入测试负载的检测结果。汇总结果还会记录 `byte_delta`、runner 运行时间、输出大小、工具版本和退出码。

Benchmark JSON 会保存 `truth_checkpoints`。`growth_leak` 按 round 记录 outstanding blocks/bytes，`delayed_free` 记录 `after_alloc` 和 `after_free` 两个阶段，便于分析运行中快照和最终结果的差异。

BCC memleak 默认执行 3 次、间隔 2 秒的快照，并自动延长被测进程的结束等待时间；`BCC_OLDER_MS` 默认设为 0，保留当前时刻的 outstanding allocation。正式实验可以通过 `BCC_INTERVAL`、`BCC_COUNT`、`BCC_POST_DELAY_MS` 和 `BCC_OLDER_MS` 调整采样窗口。没有 Benchmark 调用栈归属的非零快照会保留为 `unknown`，不会直接选取最大分配项作为测试结果。

## 重复实验

```bash
REPEATS=3 \
THREADS=2 \
ITERATIONS=2000 \
SIZE=64 \
ROUNDS=6 \
scripts/run_leak_tool_formal_experiment.sh
```

重复实验会在每轮保存独立原始结果，并生成：

- `experiment_index.csv` / `experiment_index.md`：每轮状态和输出位置
- `formal_experiment_summary.csv` / `formal_experiment_summary.md`：按工具和 case 汇总重复次数、匹配次数、匹配率、检测值和调用栈信息

## 先做脚本检查

不执行实际工具时，可以先用 dry-run 检查任务展开是否正确：

```bash
tests/test_leak_tool_compare_dry_run.sh
tests/test_leak_tool_formal_experiment_dry_run.sh
```

正式对比时应记录服务器系统版本、内核版本、编译器版本、工具版本、权限要求、运行耗时、输出文件大小和命令参数，保证不同工具的结果可复现。

## 游戏语义独立盲测

基础六类 case 用于保持历史结果的可比性。面向后续游戏类验收负载，Benchmark 另外提供一套独立的游戏语义 case。它使用抽象的资源加载、场景切换、帧循环、UI、异步缓冲和混合路径，调用栈名称保持稳定，结果可以验证工具对业务路径的归属能力。

游戏语义盲测分为三组参数 profile：

| Profile | Case | 参数重点 | 模拟场景 |
| --- | --- | --- | --- |
| `scene_scale` | `game_scene_no_leak`、`game_scene_leak`、`game_scene_growth` | 1 thread、256 iterations、4096 bytes、4 rounds | 资源加载、释放和多次场景切换 |
| `frame_hotpath` | `game_frame_no_leak`、`game_mixed` | 4 threads、5000 iterations、64 bytes | 帧循环临时对象、UI 和高频路径 |
| `async_mixed` | `game_async_delayed_free`、`game_mixed` | 2 threads、512 iterations、1024 bytes、100 ms delay | 异步资源缓冲、延迟回收和混合路径 |
| `business_paths` | `game_network_packet`、`game_audio_decode`、`game_cache_eviction` | 2 threads、1200 iterations、1024 bytes | 网络包处理、音频解码缓冲和资源缓存淘汰 |

新增的三个业务路径 case 分别模拟异常网络包未释放、音频解码失败后留下扩容缓冲，以及缓存淘汰时遗留一个长期对象。它们用于补充场景切换和帧循环之外的游戏语义路径，后续可单独观察调用栈归属和泄漏定位能力。

第二批边界 case 使用 `business_edges` profile：

| 场景 | 正常 case | 泄漏 case | 重点检查点 |
| --- | --- | --- | --- |
| 对象池 | `game_object_pool_no_leak` | `game_object_pool_leak` | `after_load`、`after_cycle` |
| 资源映射 | `game_mmap_no_leak` | `game_mmap_leak` | `after_load`、`after_unmap` |
| UI 回调 | `game_ui_callback_no_leak` | `game_ui_callback_leak` | `after_load`、`after_cancel` |
| 长时间运行 | `game_long_running_no_leak` | `game_long_running_leak` | `after_cycle_1` 到 `after_cycle_6` |

其中 `mmap` case 用 `mmap/munmap` 代替普通 heap 分配，用于观察工具对非传统映射资源的覆盖范围；UI 回调 case 用 owner 和 listener 两个相互持有的对象模拟引用环；长运行 case 每个 cycle 都保留固定数量对象，用于观察持续增长。

默认每个 profile 重复 3 次，真值由 Benchmark JSON 在工具运行前固定生成，工具输出不会参与修改真值或解析规则。先做 dry-run 检查任务展开：

```bash
REPEATS=3 TOOL=all \
  scripts/run_game_like_blind_experiment.sh --profile all --dry-run
```

确认环境和工具可用后运行完整盲测：

```bash
REPEATS=3 TOOL=all \
  scripts/run_game_like_blind_experiment.sh --profile all
```

结果写入 `results/game_like_blind/<experiment_name>/<profile>/`，每个 profile 下保留 `truth_manifest.csv`、`repeat_N` 原始输出、`experiment_config.txt`、`experiment_index.csv`、`formal_experiment_summary.csv` 和 Markdown 汇总。`truth_manifest.csv` 在工具启动前由 Benchmark 单独生成，记录本 profile 的固定真值。游戏语义 case 是面向真实游戏负载的结构化代理，后续验收仍需结合实际游戏操作或可复现回放进行验证。
