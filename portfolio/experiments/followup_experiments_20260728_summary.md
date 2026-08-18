# 2026-07-28 后续实验汇总

## 1. 实验目标

本轮完成三项补充工作：

1. 将已有结果细化到 blocks/bytes 数量误差、数量召回率、精确匹配率、调用栈命中率和 `unknown` 占比。
2. 在 BCC 已完成 attach 的条件下，测试 `growth_leak` 不同增长速度的观测结果。
3. 在多线程和高事件量负载下，比较四个工具的 CPU、RSS、运行时间和输出规模，并加入无工具基线。

实验服务器为 `Linux test server`，Benchmark 根目录为：

```text
benchmark-root
```

## 2. 数量与定位指标

汇总脚本在已有重复实验记录上增加了以下字段：

- `blocks_abs_error`、`bytes_abs_error`：检测数量与理论值的绝对误差。
- `blocks_quantity_recall`、`bytes_quantity_recall`：检测数量相对理论数量的比例。理论值为 0 时记为 `NA`。
- `blocks_error_pct`、`bytes_error_pct`：相对误差百分比。理论值为 0 时记为 `NA`。
- `blocks_exact_rate`、`bytes_exact_rate`：重复实验中完全匹配的比例。
- `benchmark_stack_hit`：输出中是否命中 Benchmark 的泄漏调用路径。
- `stack_frame_count`、`unknown_ratio`：调用栈帧数量和未知帧占比。

标准六类 case 的存在性判断仍保持稳定。扩大对象大小后，Valgrind、LSan 和 BCC 的 blocks/bytes 均与理论值匹配；heaptrack 的 blocks 仍然匹配，但在 256 bytes 和 1024 bytes 配置下分别出现 `-4000 bytes` 和 `+4000 bytes` 的稳定偏差，说明它的字节口径需要单独说明。该偏差没有影响调用路径命中和泄漏存在性判断。

## 3. BCC 增长速度实验

固定 2 个线程、每线程 2000 次分配、单次 64 bytes、6 轮、2 秒快照间隔、3 次快照和 7500 ms 结束观察窗口。BCC 提前 2000 ms attach，改变每轮之间的延迟，并重复 5 次：

| 每轮延迟 | 理论 blocks/bytes | 观测 blocks/bytes | 数量精确匹配 |
| ---: | ---: | ---: | ---: |
| 0 ms | 4000 / 256000 | 4000 / 256000 | 100% |
| 100 ms | 4000 / 256000 | 4000 / 256000 | 100% |
| 1000 ms | 4000 / 256000 | 4000 / 256000 | 100% |

结果表明，在采集已经覆盖完整分配过程的前提下，增长速度变化没有造成漏报。此前 `growth_leak` 出现的低估，主要由 attach 未覆盖负载开始阶段造成；因此后续解释 BCC 数据时需要同时报告 attach 时机、快照参数和观察窗口。

## 4. 多线程与高事件量资源开销

使用 `high_freq_no_leak`，单次分配 64 bytes，配置 1/2/4/8 线程、每线程 2000 次；另外测试 2 线程、每线程 10000 次。每个配置重复 3 次，并加入无工具基线。所有实验退出码均为 0。

主要观察如下：

- 无工具基线的 runner 时间约为 1009 ms，1/2/4/8 线程变化很小。
- LSan 的 wall-clock 约为 1.08 至 1.10 s，最大 RSS 约 46.9 MB，case 结果文件约 1.0 KB，线程数和事件量变化对其影响较小。
- heaptrack 的 runner 时间约为 1.53 至 1.61 s，wall-clock 约为 1.56 至 1.64 s，最大 RSS 约 70.2 MB，case 结果文件约 8.2 KB，保留的 profile 信息更多。
- Valgrind 的 runner 时间约为 2.61 至 2.80 s，wall-clock 约为 2.64 至 2.83 s，CPU 占用约 60% 至 64%；8 线程时最大 RSS 上升到约 90.5 MB。
- BCC 的 runner 时间约为 9.58 至 9.70 s，最大 RSS 约 155 MB。该时间包含 attach、采样和结束观察窗口，因此不能直接理解成纯内存分配路径的开销。
- 2 线程、每线程 10000 次时，四个工具的 wall-clock 和 RSS 没有出现数量级增长；BCC 的 CPU 和系统时间略有增加，其他工具变化较小。

## 5. 产物位置

本地结果位于：

```text
project-root\\组会\\followup_20260728_metrics\\followup_20260728_quantity_metrics.csv
project-root\\组会\\followup_20260728_metrics\\followup_20260728_quantity_metrics.md
project-root\\组会\\followup_20260728_metrics\\followup_20260728_stack_metrics.csv
project-root\\组会\\followup_20260728_metrics\\followup_20260728_bcc_growth_rate.csv
project-root\\组会\\followup_20260728_metrics\\followup_20260728_resource_scale.csv
project-root\\组会\\followup_20260728_metrics\\followup_20260728_resource_scale.md
```

复现实验脚本位于：

```text
project-root\\summarize_followup_20260724.py
project-root\\run_bcc_growth_rate_20260728.sh
project-root\\run_resource_scale_20260728.sh
project-root\\summarize_resource_scale_20260728.py
```

服务器原始结果和脚本同步在：

```text
benchmark-root/results/tool_compare_formal/followup_20260728_*
```

## 6. 当前结论

前三项已经形成闭环：数量指标补齐了检测结果的细粒度口径，BCC 增长实验验证了正确 attach 后的完整观测能力，资源实验给出了工具在不同线程和事件量下的工程画像。后续若继续深入，重点应放在 heaptrack 字节偏差的来源拆分，以及将 BCC 的观察窗口和采样配置纳入正式结果表的说明字段。
