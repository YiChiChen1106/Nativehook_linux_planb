# native_hook Plan B 进展记录

日期：2026-04-28

## 1. 本次新增内容

本次在 `Plan B V1` 上新增了一个最简单可运行的 `is_blocked` 分支，用来把当前路径区分成：

- `is_blocked = 0`
  - 保持当前 batching / flush 路径
- `is_blocked = 1`
  - 退化成简化同步模式

### 1.1 当前实现方式

修改位置：

- `linux_native_hook_v1/producer_hook/hook_writer.h`
- `linux_native_hook_v1/producer_hook/hook_writer.cpp`
- `linux_native_hook_v1/consumer/server_main.cpp`
- `linux_native_hook_v1/scripts/run_hook_benchmark.sh`
- `linux_native_hook_v1/scripts/run_hook_sweep.sh`

当前简化逻辑：

- consumer 现在支持 `--blocked`
- producer 在握手后会真正接收 `config.is_blocked`
- 当 `is_blocked = 1` 时：
  - 每次 `WriteRecordLocked(...)` 成功写入 record 后
  - 立即调用 `NotifyLocked()`
  - 然后等待 `read_index == write_index`

当前这不是完整 upstream `blocked` 语义，但已经具备两个关键特征：

- 路径会真正分叉
- `blocked` 模式会表现出明显更同步、更重的热路径特征

## 2. 服务器验证

服务器路径：

- `/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`

### 2.1 编译通过

执行：

```bash
cd /mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1
cmake -S . -B build
cmake --build build -j
```

结果：

- `consumer`
- `hook_preload.so`
- `perf_test_data_linux`

重新编译成功。

### 2.2 非 blocked 模式对照

consumer 配置：

- `sample_interval=1000`
- `filter_size=-1`
- `is_blocked=0`

workload 结果：

```text
threads=1 duration=1 alloc_size=32 total_iterations=4689696 throughput_ops=4689696.00
```

consumer 日志关键行为：

- `is_blocked=0`
- `avg_batch` 稳定接近 `20`

说明：

- 非 blocked 路径仍然是 batching 模式

### 2.3 blocked 模式验证

consumer 配置：

- `sample_interval=1000`
- `filter_size=-1`
- `is_blocked=1`

consumer 日志开头：

```text
flush_threshold=20 sample_interval=1000 filter_size=-1 max_stack_depth=0 clock_id=0 is_blocked=1
```

workload 结果：

```text
threads=1 duration=1 alloc_size=32 total_iterations=4117370 throughput_ops=4117370.00
```

consumer 日志尾部关键行为：

```text
wake=1 records=8246 alloc=4119 free=4117 thread_name=10 flush=8246 dropped=0 avg_batch=1
wake=1 records=8247 alloc=4119 free=4118 thread_name=10 flush=8247 dropped=0 avg_batch=1
```

说明：

- `is_blocked=1` 已经真正进 producer 路径
- 当前 blocked 模式下基本退化成“一条记录一次 flush”
- `avg_batch` 从接近 `20` 降到 `1`
- 吞吐从约 `4.69M` 下降到约 `4.12M`

这证明当前 `is_blocked` 分支已经不只是 config 占位，而是一个真实生效、可测的简化同步模式。

### 2.4 blocked 对照结果已纳入 sweep 口径

本次还把 `blocked = 0 / 1` 纳入了现有 sweep 口径，生成的对照 CSV 为：

- `linux_native_hook_v1/results/blocked_comparison_2026-04-28.csv`

本次最小对照配置：

- `threads=1`
- `size=32`
- `duration=1`
- `sample_interval=1000`
- `filter_size=-1`
- `blocked=0,1`

结果：

| blocked | with_hook_ops | overhead_pct | records | flush | avg_batch |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 4769641.00 | 72.3428 | 9540 | 477 | 20 |
| 1 | 4160795.00 | 75.8733 | 8333 | 8333 | 1 |

可以说明：

- 当前 sweep 脚本已经支持把 `blocked` 作为独立实验维度写入 CSV
- `blocked=1` 时，`avg_batch` 从 `20` 直接退化到 `1`
- `flush` 次数也从 `477` 上升到 `8333`
- 吞吐进一步下降，说明当前简化 blocked 路径已经表现出更同步、更重的热路径特征

### 2.5 blocked 与 `sample_interval` 组合 sweep

本次还补了一组更系统的 `blocked = 0 / 1` 与 `sample_interval` 组合 sweep，生成的结果文件为：

- `linux_native_hook_v1/results/blocked_sample_interval_sweep_2026-04-28.csv`

本次 sweep 固定：

- `threads=1`
- `size=32`
- `duration=1`
- `filter_size=-1`

扫点维度：

- `blocked = 0, 1`
- `sample_interval = 1, 10, 100, 1000, 10000, 100000`

关键结果：

| blocked | sample_interval | with_hook_ops | overhead_pct | records | flush | avg_batch |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 1 | 305088.00 | 98.1825 | 610784 | 30532 | 20.0047 |
| 0 | 10 | 1806771.00 | 89.2363 | 361703 | 18084 | 20.0013 |
| 0 | 100 | 4198834.00 | 74.9857 | 84060 | 4203 | 20 |
| 0 | 1000 | 4357058.00 | 74.0431 | 8721 | 436 | 20.0023 |
| 0 | 10000 | 4940412.00 | 70.5678 | 981 | 49 | 20.0204 |
| 0 | 100000 | 4805807.00 | 71.3697 | 101 | 5 | 20.2 |
| 1 | 1 | 88126.00 | 99.4750 | 176437 | 176437 | 1 |
| 1 | 10 | 741911.00 | 95.5801 | 148535 | 148535 | 1 |
| 1 | 100 | 2068401.00 | 87.6776 | 41414 | 41414 | 1 |
| 1 | 1000 | 4193330.00 | 75.0185 | 8399 | 8399 | 1 |
| 1 | 10000 | 4669863.00 | 72.1796 | 937 | 937 | 1 |
| 1 | 100000 | 4952986.00 | 70.4929 | 103 | 103 | 1 |

这组 sweep 可以说明：

- `blocked=1` 时，`avg_batch` 始终稳定在 `1`
- `blocked=0` 时，`avg_batch` 始终稳定在接近 `20`
- 在低采样阶段，`blocked=1` 的吞吐明显更差
  - 例如 `sample_interval=1` 时，从 `305088` 进一步掉到 `88126`
  - `sample_interval=10` 时，从 `1806771` 进一步掉到 `741911`
- 但随着 `sample_interval` 继续增大，两条曲线会逐步靠近
  - `sample_interval=1000` 时已经很接近
  - `sample_interval=100000` 时，两者基本收敛在约 `4.8M ~ 5.0M`

这说明：

- `blocked` 模式的额外成本，主要在“记录仍然很多”的阶段更明显
- 当 `sample_interval` 把 record 流量继续压低以后，`blocked / non-blocked` 的差异会明显收敛
- 也就是说，`blocked` 更像是在高 record 密度阶段进一步放大了当前热路径同步发送成本

### 2.6 blocked 与 `filter_size` 组合 sweep

本次还补了一组 `blocked = 0 / 1` 与 `filter_size` 组合 sweep，生成的结果文件为：

- `linux_native_hook_v1/results/blocked_filter_size_sweep_2026-04-28.csv`

本次 sweep 固定：

- `threads=1`
- `sample_interval=1`
- `duration=1`

扫点维度：

- `blocked = 0, 1`
- `size = 32, 64, 256`
- `filter_size = -1, 32, 64, 128, 256`

几个代表点：

#### `size = 32`

| blocked | filter_size | with_hook_ops | records | flush | avg_batch |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | -1 | 314056.00 | 628742 | 31433 | 20.0026 |
| 1 | -1 | 108328.00 | 216881 | 216881 | 1 |
| 0 | 64 | 5268921.00 | 0 | 0 | 0 |
| 1 | 64 | 5269619.00 | 4 | 4 | 1 |

#### `size = 64`

| blocked | filter_size | with_hook_ops | records | flush | avg_batch |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 64 | 311481.00 | 623584 | 31178 | 20.0008 |
| 1 | 64 | 113612.00 | 227455 | 227455 | 1 |
| 0 | 128 | 5257660.00 | 0 | 0 | 0 |
| 1 | 128 | 5275279.00 | 4 | 4 | 1 |

#### `size = 256`

| blocked | filter_size | with_hook_ops | records | flush | avg_batch |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 256 | 309590.00 | 619802 | 30965 | 20.0162 |
| 1 | 256 | 84501.00 | 169175 | 169175 | 1 |

这组 sweep 可以说明：

- 只要当前 `filter_size` 还没有真正把 workload 挡住，`blocked=1` 就会明显更重
  - `avg_batch` 固定退化到 `1`
  - `flush` 次数也会大幅增加
- 一旦 `filter_size` 足够大，已经把当前 workload 基本挡掉，`blocked / non-blocked` 就会快速收敛
  - 例如 `size=32, filter_size=64`
  - 以及 `size=64, filter_size=128`
- 当前 blocked 路径下偶尔还会看到极少量残余 record
  - 例如 `records=4`
  - 这说明在 workload 主体已被过滤掉后，路径里仍有极少量残余事件
  - 但整体已经接近“无有效 workload record”状态

这进一步支持前面的判断：

- `blocked` 的额外成本，主要还是在“记录仍然大量产生”的阶段更明显
- 当 sample/filter 已经把流量大幅压低后，`blocked / non-blocked` 的差距会明显缩小

## 3. 当前状态汇总

如果按最初那条推进顺序来看：

- `sample_interval`
  - 已做最简单版本
- `filter_size`
  - 已做最简单版本
- `is_blocked`
  - 已做最简单版本
- `stack capture`
  - 仍未做

## 4. 当前边界

当前 `is_blocked` 版本的边界：

- 它是一个简化版“同步/阻塞发送”分支
- 还不是完整 upstream `PutWithPayloadSync(...)` 语义复刻
- 它的价值主要在于：
  - 让路径真正分叉
  - 让后面可以把 `blocked / non-blocked` 作为实验维度加入对照

所以更准确的说法是：

> 当前 `is_blocked` 已经从字段占位推进成了一个可运行、可测的简化同步分支，但还不是完整 upstream blocked 模式。

## 5. 下一步建议

现在最自然的下一步，不是先跳去 stack capture，而是先把 `blocked` 也纳入现有 sweep / ablation 口径：

- `blocked = 0 / 1`
- 配合当前的 `sample_interval`
- 配合当前的 `filter_size`

这样可以先看：

- `blocked` 模式本身到底会不会显著放大当前 producer 热路径成本
- 以及它和 sample/filter 组合后，是否会暴露出更明确的瓶颈层次
