# native_hook pid/tid cache overall impact

## 实验目的

这轮不再拆新的 micro segment，而是直接回答两个问题：

1. `LNHV1_PID_TID_CACHE=1` 对 Stage 5/6 本身能带来多少吞吐提升。
2. 这个优化反映到整体 producer hot path 后，能把相对 no-hook baseline 的 overhead 降低多少。

正式结果来自服务器 `pink`：

- 目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_pid_tid_overall_impact_server_2026-05-25.csv`
- 本地副本：`F:\codex_workspace\native_hook\linux_native_hook_v1\results\hook_pid_tid_overall_impact_server_2026-05-25.csv`

实验参数沿用前面 Plan B：

- `threads=1,4`
- `size=32`
- `duration=5`
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- `tracking_mode=global`

## 结果一：Stage 5/6 直接收益

| threads | stage | cache off | cache on | speedup |
|---:|---|---:|---:|---:|
| 1 | Stage 5 record_write | 0.299 M ops/s | 0.898 M ops/s | 3.00x |
| 1 | Stage 6 notify | 0.278 M ops/s | 0.811 M ops/s | 2.92x |
| 4 | Stage 5 record_write | 0.079 M ops/s | 0.291 M ops/s | 3.67x |
| 4 | Stage 6 notify | 0.075 M ops/s | 0.192 M ops/s | 2.56x |

结论：pid/tid cache 对 Stage 5/6 的累计 producer 路径是明确有效的优化。它不是只改善一个孤立函数，而是能直接体现在 record_write 和 notify 两个阶段的吞吐上。

## 结果二：整体 producer hot path overhead

| threads | stage | cache off overhead | cache on overhead | overhead 下降 |
|---:|---|---:|---:|---:|
| 1 | Stage 5 record_write | 98.27% | 94.80% | 3.47 pct points |
| 1 | Stage 6 notify | 98.39% | 95.31% | 3.08 pct points |
| 4 | Stage 5 record_write | 99.40% | 97.78% | 1.61 pct points |
| 4 | Stage 6 notify | 99.43% | 98.54% | 0.89 pct points |

如果用“相对 no-hook baseline 损失的吞吐被恢复多少”来表达：

| threads | stage | lost throughput recovered |
|---:|---|---:|
| 1 | Stage 5 record_write | 3.53% |
| 1 | Stage 6 notify | 3.14% |
| 4 | Stage 5 record_write | 1.62% |
| 4 | Stage 6 notify | 0.90% |

这个口径比直接 speedup 更保守，也更适合回答“对整个 producer hot path 有多少改善”。它说明：pid/tid cache 是 Stage 5/6 内部的大优化，但还没有把整体路径拉回到接近 no-hook，因为前面的 mutex、tracking，以及后面的 writer/ring/notify 共享路径仍然在。

## 完整 ablation 曲线参考

### 1 thread

| stage | throughput |
|---|---:|
| baseline no hook | 17.274 M ops/s |
| Stage 1 hook_entry | 11.650 M ops/s |
| Stage 2 guard | 6.804 M ops/s |
| Stage 3 mutex | 3.486 M ops/s |
| Stage 4 tracking | 1.183 M ops/s |
| Stage 5 cache off | 0.299 M ops/s |
| Stage 5 cache on | 0.898 M ops/s |
| Stage 6 cache off | 0.278 M ops/s |
| Stage 6 cache on | 0.811 M ops/s |

### 4 threads

| stage | throughput |
|---|---:|
| baseline no hook | 13.152 M ops/s |
| Stage 1 hook_entry | 9.526 M ops/s |
| Stage 2 guard | 10.071 M ops/s |
| Stage 3 mutex | 1.756 M ops/s |
| Stage 4 tracking | 0.400 M ops/s |
| Stage 5 cache off | 0.079 M ops/s |
| Stage 5 cache on | 0.291 M ops/s |
| Stage 6 cache off | 0.075 M ops/s |
| Stage 6 cache on | 0.192 M ops/s |

## 组会表述

可以这样讲：

> pid/tid cache 对 Stage 5/6 是一个很确定的优化，Stage 5 大概提升 3.0x 到 3.7x，Stage 6 大概提升 2.6x 到 2.9x。但如果放到整个 producer hot path 里看，overhead 只下降了几个百分点。这说明 pid/tid 是 Stage 5 里的明确热点，但不是唯一问题；剩下的瓶颈还在 mutex、tracking，以及 writer/ring/notify 共享路径。

下一步如果要继续做“整体收益”实验，应该把 `pid/tid cache` 和 tracking/backend 改动组合起来看：

- global + cache off
- global + cache on
- sharded + cache on
- thread_local_fallback + cache on
- thread_local_only + cache on 作为上界对照

这样可以把“单个优化有效”推进到“组合后完整 producer path 能恢复多少吞吐”。
