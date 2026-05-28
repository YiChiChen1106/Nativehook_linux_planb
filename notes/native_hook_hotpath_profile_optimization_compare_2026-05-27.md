# native_hook hot-path profile: 优化前后对比

## 实验目的

这组实验用 `LNHV1_HOTPATH_PROFILE=1` 直接在 producer hot path 内打点，回答两个问题：

- `pid/tid cache` 到底让 Stage 5/6 的 metadata 路径少花了多少时间。
- tracking backend 从 `global` 换到 `sharded` / `thread_local_fallback` 后，Stage 4 和 cache-on Stage 6 的等待时间有没有下降。

注意：profile 模式会扰动热路径，因此这里的数值用于 segment attribution，不作为最终 overhead 数字。

## 实验设置

- 服务器：`pink`
- 目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_hotpath_profile_optimization_compare_server_2026-05-27.csv`
- 本地副本：`F:\codex_workspace\native_hook\planb_project_page\linux_native_hook_v1\results\hook_hotpath_profile_optimization_compare_server_2026-05-27.csv`
- workload：`perf_test_data_linux`
- 参数：`threads=1,4`，`size=32`，`duration=5`
- profile 组合：Stage 4/5/6 x `global,sharded,thread_local_fallback` x `pid_tid_cache=0,1`

## 1. pid/tid cache 对 Stage 5/6 的影响

同一 tracking backend 固定为 `global`，只比较 `pid_tid_cache=0` 和 `pid_tid_cache=1`。

| Stage | Threads | cache off ns/op | cache on ns/op | 省下 ns/op | profile 吞吐提升 |
|---:|---:|---:|---:|---:|---:|
| 5 | 1 | 4848.49 | 2120.87 | 2727.62 | 2.19x |
| 6 | 1 | 4929.10 | 2293.17 | 2635.93 | 2.07x |
| 5 | 4 | 83503.77 | 47226.43 | 36277.34 | 1.75x |
| 6 | 4 | 80355.55 | 40325.72 | 40029.83 | 1.98x |

metadata segment 本身的变化更直接：

| Stage | Threads | cache off pid+tid ns/op | cache on pid+tid ns/op |
|---:|---:|---:|---:|
| 5 | 1 | 2542.15 | ~0 |
| 6 | 1 | 2481.82 | ~0 |
| 5 | 4 | 6916.27 | ~0.03 |
| 6 | 4 | 6794.11 | ~0.03 |

结论：`pid/tid cache` 基本消掉了热路径里重复 `getpid/gettid` 的直接 metadata 成本。Stage 总耗时下降比 metadata segment 还大，说明 cache 也缩短了持锁区间，间接降低了多线程竞争。

## 2. tracking backend 对 Stage 4 的影响

Stage 4 只看 tracking，本表固定 `pid_tid_cache=1`；Stage 4 本身不依赖 pid/tid，这里只是保持脚本字段一致。

| Threads | Mode | Stage 4 ns/op | profile throughput ops/s |
|---:|---|---:|---:|
| 1 | global | 1390.88 | 632877 |
| 1 | sharded | 1413.16 | 624509 |
| 1 | thread_local_fallback | 1380.28 | 639097 |
| 4 | global | 12184.62 | 319553 |
| 4 | sharded | 11886.43 | 327705 |
| 4 | thread_local_fallback | 3563.05 | 1033125 |

4 线程下关键 segment：

| Mode | lock wait ns/op | insert ns/op | lookup ns/op | erase ns/op |
|---|---:|---:|---:|---:|
| global | 7460.06 | 501.21 | 330.89 | 332.00 |
| sharded | 7198.07 | 622.79 | 306.48 | 400.48 |
| thread_local_fallback | 1205.07 | 549.68 | 120.54 | 273.60 |

结论：`sharded` 在 profile 口径下对 Stage 4 改善不大，主要因为当前负载的地址分布和 profile 扰动下仍有明显 shard wait。`thread_local_fallback` 把 4 线程 Stage 4 从 12184.62 ns/op 降到 3563.05 ns/op，主要收益来自大幅减少 tracking lock wait。

## 3. cache-on Stage 6 下 tracking 优化的整体影响

固定 `pid_tid_cache=1`，比较 Stage 6 的完整 producer hot path reference。

| Threads | Mode | Stage 6 ns/op | profile throughput ops/s |
|---:|---|---:|---:|
| 1 | global | 2293.17 | 403747 |
| 1 | sharded | 2767.74 | 338440 |
| 1 | thread_local_fallback | 2806.88 | 333849 |
| 4 | global | 40325.72 | 97338 |
| 4 | sharded | 18479.63 | 212440 |
| 4 | thread_local_fallback | 16421.83 | 238673 |

4 线程下，`thread_local_fallback` 相比 `global`：

- Stage 6 总路径：`40325.72 -> 16421.83 ns/op`，省 `23903.89 ns/op`。
- profile 吞吐：`97338 -> 238673 ops/s`，约 `2.45x`。
- tracking lock wait：`27800.26 ns/op` 的 global writer wait 变成 `213.18 ns/op` 的 fallback shard wait。
- 但仍有 `8971.74 ns/op` 的 writer/ring mutex wait，说明 Stage 6 后面还卡在 writer/ring 共享路径。

## 4. 优化前到当前优化后的整体 profile 对比

如果把“优化前”定义成 `global + pid_tid_cache=0`，把“优化后”定义成 `thread_local_fallback + pid_tid_cache=1`：

| Stage | Threads | before ns/op | after ns/op | 省下 ns/op | profile 吞吐提升 |
|---:|---:|---:|---:|---:|---:|
| 5 | 1 | 4848.49 | 2638.97 | 2209.52 | 1.78x |
| 6 | 1 | 4929.10 | 2806.88 | 2122.22 | 1.71x |
| 5 | 4 | 83503.77 | 17057.01 | 66446.76 | 4.84x |
| 6 | 4 | 80355.55 | 16421.83 | 63933.72 | 4.84x |

解释口径：

- 单线程主要收益来自 `pid/tid cache`；tracking backend 的结构性优化在单线程下不一定赚钱。
- 4 线程收益来自两部分叠加：`pid/tid cache` 缩短 metadata 和持锁区间，`thread_local_fallback` 大幅减少 tracking lock wait。
- Stage 6 仍然有 writer/ring mutex wait，所以下一步如果继续优化完整 producer hot path，重点应转向 writer/ring 共享路径，而不是继续拆 pid/tid。
