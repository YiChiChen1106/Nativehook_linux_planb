# native_hook tracking overall impact

## 实验目的

这轮不再继续拆 `insert / lookup / erase`，而是把现有 tracking backend 放回完整 producer hot path 里，看它们对 Stage 4、cache-on Stage 5/6 的整体影响。

正式结果来自服务器 `pink`：

- 目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_tracking_overall_impact_server_2026-05-25.csv`
- 本地副本：`F:\codex_workspace\native_hook\linux_native_hook_v1\results\hook_tracking_overall_impact_server_2026-05-25.csv`

实验参数：

- `threads=1,4`
- `size=32`
- `duration=5`
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- Stage 5/6 使用 `LNHV1_PID_TID_CACHE=1`

## 结果一：Stage 4 tracking 本身

| threads | global | sharded | thread_local_fallback | thread_local_only |
|---:|---:|---:|---:|---:|
| 1 | 1.234 M | 1.231 M | 0.821 M | 1.425 M |
| 4 | 0.425 M | 0.400 M | 0.414 M | 4.403 M |

相对 global：

| threads | sharded | thread_local_fallback | thread_local_only |
|---:|---:|---:|---:|
| 1 | 1.00x | 0.67x | 1.15x |
| 4 | 0.94x | 0.97x | 10.37x |

结论：`thread_local_only` 在四线程 Stage 4 上界非常高，说明共享 tracking 结构确实是强瓶颈。但它不处理 cross-thread free，所以不能当作正式语义。`thread_local_fallback` 没有吃到这个上界，说明朴素 fallback mirror 的双写/双查成本会把收益吃掉。

## 结果二：对 cache-on Stage 5/6 的整体贡献

### Stage 5 record_write cache-on

| threads | global | sharded | thread_local_fallback | thread_local_only |
|---:|---:|---:|---:|---:|
| 1 | 0.915 M | 0.805 M | 0.595 M | 0.868 M |
| 4 | 0.315 M | 0.332 M | 0.330 M | 0.373 M |

相对 global：

| threads | sharded | thread_local_fallback | thread_local_only |
|---:|---:|---:|---:|
| 1 | 0.88x | 0.65x | 0.95x |
| 4 | 1.05x | 1.04x | 1.18x |

### Stage 6 notify cache-on

| threads | global | sharded | thread_local_fallback | thread_local_only |
|---:|---:|---:|---:|---:|
| 1 | 0.785 M | 0.718 M | 0.537 M | 0.740 M |
| 4 | 0.211 M | 0.283 M | 0.278 M | 0.315 M |

相对 global：

| threads | sharded | thread_local_fallback | thread_local_only |
|---:|---:|---:|---:|
| 1 | 0.91x | 0.68x | 0.94x |
| 4 | 1.34x | 1.32x | 1.49x |

结论：tracking backend 的整体收益主要体现在四线程完整路径，尤其 Stage 6。单线程下 sharded / fallback 反而略慢，说明这类结构优化主要是在缓解并发共享路径，不是降低单线程常数成本。

## 组会表述

可以这样讲：

> pid/tid cache 之后，我把 tracking 的几种实现形态放回完整 Stage 4/5/6 里测整体收益。结果显示，四线程下 Stage 6 从 global 到 sharded 大约 1.34x，thread_local_only 上界大约 1.49x；但 thread_local_fallback 没有接近 Stage 4 的 thread-local 上界。我的理解是，per-thread tracking 方向有潜力，但不能用现在这种朴素 fallback mirror，需要重新设计更低成本的 ownership/fallback。

下一步建议不是继续调 shard 数，也不是继续拆 unordered_set 的单个操作，而是做低成本 per-thread ownership：

- alloc 记录 owner/thread context，但避免每次都双写全局 mirror。
- free 先查本地；local miss 时再走低频 fallback。
- 单独构造 cross-thread free workload，量化 fallback 触发率和代价。
