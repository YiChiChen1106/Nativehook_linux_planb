# native_hook fixed-work hot-path profile

## 实验目的

这组实验把 workload 从“固定 duration”换成“固定工作量”，用于直接回答：

> 同样跑 100 万次 malloc/free pair，优化前后实际少花多少秒？

profile 仍然开启 `LNHV1_HOTPATH_PROFILE=1`，所以这组结果适合解释“固定工作量下的 profile 墙钟时间”和“内部 segment 时间”，不作为最终普通运行 overhead。

## 改动

- `perf_test_data_linux` 新增参数：`--iters-per-thread n`
- 新增脚本：`scripts/run_hook_fixed_work_profile.sh`
- 固定总工作量：`LNHV1_FIXED_TOTAL_OPS=1000000`
- `threads=1` 时：每线程 `1000000` 次
- `threads=4` 时：每线程 `250000` 次，总量仍是 `1000000` 次

正式 CSV：

- 服务器：`pink`
- 路径：`results/hook_fixed_work_profile_server_2026-05-27.csv`
- Stage 4 补充 CSV：`results/hook_fixed_work_stage4_profile_server_2026-05-27.csv`
- 本地副本：`F:\codex_workspace\native_hook\planb_project_page\linux_native_hook_v1\results\hook_fixed_work_profile_server_2026-05-27.csv`

## 固定工作量结果：墙钟时间

“优化前”定义为 `global + pid_tid_cache=0`。下面每行都是固定 100 万次 malloc/free pair 的完成时间。

| Threads | Stage | Scenario | Before | After | Saved | Speedup |
|---:|---:|---|---:|---:|---:|---:|
| 1 | 5 | global cache on | 4.91s | 2.30s | 2.61s | 2.14x |
| 1 | 6 | global cache on | 5.09s | 2.46s | 2.63s | 2.07x |
| 4 | 5 | global cache on | 22.71s | 12.64s | 10.07s | 1.80x |
| 4 | 6 | global cache on | 23.08s | 12.93s | 10.15s | 1.79x |
| 4 | 5 | sharded cache on | 22.71s | 4.15s | 18.56s | 5.48x |
| 4 | 6 | sharded cache on | 23.08s | 4.70s | 18.38s | 4.91x |
| 4 | 5 | thread_local_fallback cache on | 22.71s | 4.21s | 18.50s | 5.40x |
| 4 | 6 | thread_local_fallback cache on | 23.08s | 4.55s | 18.54s | 5.08x |

Stage 4 单独补跑固定工作量后：

| Threads | Stage | Scenario | Before | After | Saved | Speedup |
|---:|---:|---|---:|---:|---:|---:|
| 4 | 4 | thread_local_fallback cache on | 3.08s | 0.89s | 2.18s | 3.44x |

## 累计热路径 segment 口径

同样固定 100 万次 op，如果看 profile segment 的累计热路径时间：

| Threads | Stage | Before ns/op | After ns/op | Saved ns/op | Saved / 1M ops |
|---:|---:|---:|---:|---:|---:|
| 1 | 5 | 4771.39 | 2619.35 | 2152.04 | 2.15s |
| 1 | 6 | 4957.22 | 2797.01 | 2160.21 | 2.16s |
| 4 | 5 | 84653.32 | 16470.27 | 68183.05 | 68.18s |
| 4 | 6 | 86921.34 | 17750.95 | 69170.39 | 69.17s |

这里的 `Saved / 1M ops` 是多线程累计热路径时间，不等于墙钟时间。对 leader 展示“省了多少秒”时，主讲上一节的墙钟时间；这一节用于解释内部 segment 为什么下降。

## 组会口径

可以直接讲：

> 我补了固定工作量实验，不再只用 5 秒 duration 反推。固定做 100 万次 malloc/free pair 后，4 线程 Stage 6 从 23.08 秒降到 4.55 秒，省 18.54 秒，约 5.08 倍。单独打开 pid/tid cache 可以先把 23.08 秒降到 12.93 秒；再换 tracking backend，进一步降到 4.55 秒左右。

边界也要讲清楚：

- 这组开启了 `LNHV1_HOTPATH_PROFILE=1`，用于回答 profile 口径下的固定工作量时间。
- 如果要最终 overhead 数字，仍然看不开 profile 的普通 throughput/overall impact 实验。
