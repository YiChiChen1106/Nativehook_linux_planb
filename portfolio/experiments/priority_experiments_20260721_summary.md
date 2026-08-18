# 2026-07-21 优先级实验汇总

## 1. 实验范围

本轮实验围绕四个 Linux 内存泄漏检测工具展开：Valgrind Memcheck、LSan、heaptrack 和 BCC memleak。实验不涉及 NativeHook 或 Plan B，目标是补充三类数据：BCC 运行参数对检测结果的影响、线程数和负载规模扩大后的稳定性、以及工具的工程资源开销。

实验均在 `Linux test server` 服务器上完成，Benchmark 根目录为：

```text
benchmark-root
```

所有结果均保存在：

```text
results/tool_compare_formal/
```

本轮共完成 118 次重复实验，所有实验的退出码均为 `0`。

## 2. BCC 快照参数扫描

### 2.1 实验设置

固定 2 个线程，每个线程执行 2000 次分配，单次分配 64 bytes，使用 `growth_leak` 场景，理论上应保留 4000 个对象，共 256000 bytes。扫描两组观察参数：

| 参数 | 取值 |
| --- | --- |
| 快照间隔 | 1 s、2 s |
| 快照次数 | 6、9、12 |
| 每组重复 | 3 次 |

### 2.2 结果

| 参数组合 | 观测 blocks 范围 | 观测 bytes 范围 | 是否稳定匹配预期 |
| --- | ---: | ---: | --- |
| 1 s / 6 次 | 2664 | 170496 | 否 |
| 1 s / 9 次 | 2663-2664 | 170432-170496 | 否 |
| 1 s / 12 次 | 2664-3332 | 170496-213248 | 否 |
| 2 s / 6 次 | 2663-3332 | 170432-213248 | 否 |
| 2 s / 9 次 | 2664-3331 | 170496-213184 | 否 |
| 2 s / 12 次 | 2664-3332 | 170496-213248 | 否 |

### 2.3 观察

BCC 的结果会随着快照间隔和快照次数变化：部分组合从约 2664 个对象上升到约 3332 个对象，但仍没有稳定达到理论值 4000。说明在持续增长负载中，BCC 的快照时点、事件追踪窗口或 BPF map 更新过程会影响最终 outstanding 数量。这个结果不能直接说明测试负载只产生了 2664 或 3332 个泄漏对象，而是说明当前快照式观测对完整事件集合存在低估。

## 3. 线程数与负载规模扩展

### 3.1 BCC growth_leak

将线程数从 1 扩展到 8，每个线程的迭代次数和分配大小保持不变，每个线程数重复 3 次。

| 线程数 | 理论 blocks | 理论 bytes | 观测 blocks | 观测 bytes | 匹配率 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 2000 | 128000 | 1332-1666 | 85248-106624 | 0% |
| 2 | 4000 | 256000 | 2663-3332 | 170432-213248 | 0% |
| 4 | 8000 | 512000 | 5326-5328 | 340864-340992 | 0% |
| 8 | 16000 | 1024000 | 10656-12648 | 681984-809472 | 0% |

观测数量总体随线程数增加而增加，呈现出大致线性扩展关系，但各规模下均存在低估，且 8 线程时重复结果的波动更明显。这表明 BCC 在持续增长场景中的主要问题是完整性和快照时机，而非完全无法感知增长趋势。

### 3.2 四工具 definite_leak

对 Valgrind、LSan、heaptrack 和 BCC memleak 分别测试 1、2、4、8 个线程，每个配置重复 3 次。该场景的理论泄漏量随线程数线性增加。

| 工具 | 1 线程 | 2 线程 | 4 线程 | 8 线程 | 规模扩展结论 |
| --- | --- | --- | --- | --- | --- |
| Valgrind | 100% | 100% | 100% | 100% | 四种规模均精确匹配 |
| LSan | 100% | 100% | 100% | 100% | 四种规模均精确匹配 |
| heaptrack | 100% | 100% | 100% | 0% | 8 线程少报 4000 bytes |
| BCC memleak | 100% | 100% | 100% | 100% | definite_leak 场景均精确匹配 |

这里需要区分两类结果：BCC 在 `definite_leak` 场景中可以精确匹配，因为目标对象的生命周期和采样窗口更容易对齐；同一个工具在 `growth_leak` 场景中仍然会低估，说明持续增长场景对快照时点更敏感。

heaptrack 在 8 线程时报告 16000 blocks、1020000 bytes，理论值为 1024000 bytes，差异为 4000 bytes。该偏差只在最大规模下出现，需要在后续分析中继续确认 profile 字节口径和结果解析边界。

## 4. 工程资源指标

### 4.1 实验设置

使用 `high_freq_no_leak` 场景，2 个线程、每线程 2000 次分配、单次 64 bytes、6 轮负载，每个工具重复 5 次。通过 `/usr/bin/time -v` 记录 CPU 占用、最大 RSS、用户态时间、系统态时间和 wall-clock 时间，同时记录 runner 执行时间和输出文件大小。

### 4.2 汇总结果

下表为 5 次重复的均值和标准差。BCC 的 wall-clock 时间包含采样观察窗口，因此不能直接与其他工具的纯负载执行时间等同比较。

| 工具 | CPU | 最大 RSS | wall-clock | runner 时间 | 输出大小 |
| --- | ---: | ---: | ---: | ---: | ---: |
| LSan | 7.40% +/- 0.55% | 46940 KB +/- 274 KB | 1.07 s +/- 0.01 s | 1053.80 ms +/- 1.79 ms | 1044 B +/- 1 B |
| heaptrack | 23.00% +/- 0.71% | 70206 KB +/- 108 KB | 1.55 s +/- 0.02 s | 1528.60 ms +/- 21.24 ms | 8228 B +/- 20 B |
| Valgrind | 59.00% +/- 0.00% | 84052 KB +/- 0 KB | 2.46 s +/- 0.00 s | 2439.00 ms +/- 5.29 ms | 1934 B +/- 0 B |
| BCC memleak | 10.00% +/- 0.00% | 155133 KB +/- 232 KB | 9.58 s +/- 0.01 s | 9557.80 ms +/- 4.44 ms | 4095 B +/- 0 B |

### 4.3 观察

在本轮配置下，LSan 的 CPU、内存和运行时间均较低；heaptrack 的输出文件较大，体现了 profile 信息保留较多；Valgrind 的 CPU 占用和运行时间较高；BCC 的最大 RSS 和 wall-clock 时间较高，主要与内核追踪和采样观察窗口有关。上述结果是当前 Benchmark 配置下的工程观测值，后续仍需结合不同负载规模和采样策略进行解释。

## 5. 结果结论

1. `definite_leak` 场景下，四个工具在 1、2、4 线程规模中均完成精确匹配；8 线程下除 heaptrack 少报 4000 bytes 外，其余工具仍保持精确匹配。
2. BCC 在 `growth_leak` 场景下能够感知随线程数增加而增长的 outstanding 数量，但不同快照参数下存在稳定低估，当前更适合表述为趋势观测结果，不能直接作为完整泄漏对象计数。
3. 当前资源数据给出了四个工具的相对工程画像：LSan 较轻量，heaptrack 保留较多 profile 输出，Valgrind 运行开销较高，BCC 的观察窗口和内核追踪带来较长 wall-clock 时间与较高 RSS。
4. 本轮数据已经足以支撑组会对“检测准确性、规模扩展表现和工程开销”三条线进行汇报，但 BCC growth 的低估原因和 heaptrack 大规模字节偏差仍需单独复核。

## 6. 组会前补充实验

### 6.1 BCC attach 时机与结束后观察窗口

先做了 3 组对照，每组 3 次：负载快速完成，`BCC_START_DELAY_MS=0`，结束后保留时间为 7000、9000、12000 ms。由于分配在 BCC 完成 attach 前已经结束，BCC 最终只能看到未知栈或空快照，这组结果不能用于判断完整泄漏检测能力，但证明了采集开始时机本身会影响结果。

随后让 Benchmark 先等待 2000 ms，确保 BCC 已完成 attach，再开始分配；快照间隔固定为 2 s、快照次数固定为 3 次，结束后保留时间仍取 7000、9000、12000 ms。三组配置的 3 次重复均得到：

```text
4000 blocks / 256000 bytes
```

这说明在采集链路已经覆盖完整分配过程时，单纯延长负载结束后的保留时间不会继续改变最终结果。此前 `growth_leak` 的低估主要与 BCC 是否覆盖了增长过程、采样时点和持续增长期间的事件追踪有关。

### 6.2 heaptrack 8 线程复核

重新执行 8 线程 `definite_leak`，重复 3 次，三次结果完全一致：

```text
detected_blocks = 16000
detected_bytes  = 1020000
expected_bytes  = 1024000
byte_delta      = -4000
```

因此该偏差可以稳定复现，当前应记录为最大规模下的 heaptrack 字节统计或解析口径偏差，不能归因于单次运行波动。

### 6.3 Precision 和 recall

基于第一轮六类 case 的 3 次重复结果，按 case-level 泄漏存在性计算指标：

- 正样本：`definite_leak`、`growth_leak`、`mixed`；
- 负样本：`no_leak`、`delayed_free`、`high_freq_no_leak`。

四个工具均得到 18 个有效样本，其中 TP=9、FP=0、FN=0、TN=9：

| 工具 | precision | recall | 说明 |
| --- | ---: | ---: | --- |
| Valgrind | 100% | 100% | 六类 case 均无误报或漏报 |
| LSan | 100% | 100% | 六类 case 均无误报或漏报 |
| heaptrack | 100% | 100% | 使用 SI 字节口径结果 |
| BCC memleak | 100% | 100% | 第一轮标准 attach 配置下结果稳定 |

这些指标反映的是当前 Benchmark 和 case-level 标签下的检测存在性，不等同于真实业务数据上的泛化精度。BCC 在不同 attach 时机和持续增长参数下的敏感性，需要结合前面的窗口实验一起说明。

### 6.4 无工具基线

在与资源实验相同的 `high_freq_no_leak` 配置下，补跑 5 次无工具 Benchmark：2 个线程、每线程 2000 次分配、单次 64 bytes、post delay 1000 ms。

| 指标 | 无工具基线 |
| --- | ---: |
| runner 时间 | 1008.00 ms +/- 1.22 ms |
| wall-clock | 1.00 s +/- 0.00 s |
| 最大 RSS | 3825 KB +/- 253 KB |
| CPU | 0.00% |

与基线相比，当前配置下的 runner 时间约为：LSan 1.05 倍、heaptrack 1.52 倍、Valgrind 2.42 倍、BCC memleak 9.48 倍。BCC 的比值包含采样观察窗口，不能直接理解成纯分配路径慢了 9.48 倍。基线负载本身很短，`time -v` 的 CPU 百分比被取整为 0%，CPU 数据只作为参考，时间和 RSS 更适合用于本轮对比。

## 7. 新增产物

```text
project-root\\priority_group3_20260721.sh
project-root\\summarize_priority_metrics_20260721.py
project-root\\summarize_priority_baseline_20260721.py
project-root\\组会\\priority_precision_recall_20260721.csv
project-root\\组会\\priority_precision_recall_20260721.md
project-root\\组会\\priority_baseline_metrics_20260721.csv
project-root\\组会\\priority_baseline_metrics_20260721.md
project-root\\组会\\priority_overhead_comparison_20260721.csv
```

服务器新增结果位于：

```text
benchmark-root/results/tool_compare_formal/priority_20260721_bcc_end_window_*
benchmark-root/results/tool_compare_formal/priority_20260721_heaptrack_definite_t8_recheck
benchmark-root/results/tool_compare_formal/priority_20260721_baseline_high_freq
benchmark-root/results/tool_compare_formal/priority_20260721_precision_recall.*
```

## 6. 产物位置

本地汇总文件：

```text
project-root\\组会\\priority_experiments_20260721_summary.md
project-root\\组会\\priority_resource_metrics_20260721.csv
project-root\\组会\\priority_resource_metrics_20260721.md
```

服务器原始结果：

```text
benchmark-root/results/tool_compare_formal/
```
