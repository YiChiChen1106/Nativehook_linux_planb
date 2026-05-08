# native_hook Plan B tracking optimization 总结

## 这轮实验想回答什么

前面的 Stage 4 tracking sub-ablation 已经说明：`sample/filter`、alloc 侧 `insert`、free 侧 `lookup/erase` 都有成本。pid/tid cache 优化完成后，producer hot path 里剩下最明确的结构性问题之一，就是当前 tracking 仍然是：

> 单个 `pthread_mutex` + 单个 `std::unordered_set<uint64_t>`

所以这轮不继续拆 `insert/lookup/erase` 微段，而是做一个实现形态对照：把 tracking 改成 64 个 shard，每个 shard 有自己的 mutex 和 unordered_set，看看 Stage 4、cache-on Stage 5/6 是否整体提升。

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_tracking_optimization_server_2026-05-08.csv`
- workload：`perf_test_data_linux`
- allocation size：32B
- duration：5s
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- 线程数：1 线程、4 线程
- Stage 5/6 reference 使用 `LNHV1_PID_TID_CACHE=1`

这组实验仍然是 producer hot-path 定位实验，不代表最终 native_hook overhead。

## 实现对照

新增开关：

- `LNHV1_TRACKING_MODE=global`：旧路径，单个 global mutex + 单个 unordered_set
- `LNHV1_TRACKING_MODE=sharded`：64 个 tracking shard，按地址 hash 到 shard
- 默认不设置时仍是 `global`

Stage 4 sharded 路径不再使用 writer global mutex，只锁对应 tracking shard。Stage 5/6 sharded 路径里，tracking lock 和 writer/ring lock 分离；ring write、notify 和 connection 仍然保留 writer mutex。

## 单线程结果

| 路径 | tracking mode | throughput_ops | overhead vs baseline | 相对 global |
|---|---|---:|---:|---:|
| baseline no hook | none | 17.04M | 0.00% | - |
| Stage 3 mutex | none | 6.92M | 59.39% | - |
| Stage 4 tracking | global | 1.66M | 90.28% | 1.00x |
| Stage 4 tracking | sharded | 1.79M | 89.48% | 1.08x |
| Stage 5 cache-on | global | 1.36M | 92.00% | 1.00x |
| Stage 5 cache-on | sharded | 1.32M | 92.28% | 0.97x |
| Stage 6 cache-on | global | 1.06M | 93.76% | 1.00x |
| Stage 6 cache-on | sharded | 1.07M | 93.69% | 1.01x |

单线程里，Stage 4 sharded 有约 8% 小幅提升；Stage 5 cache-on 略低，Stage 6 cache-on 基本持平。这说明 sharding 能减少一部分 tracking 成本，但在没有多线程竞争时，它不是无条件更快。

## 四线程结果

| 路径 | tracking mode | throughput_ops | overhead vs baseline | 相对 global |
|---|---|---:|---:|---:|
| baseline no hook | none | 22.14M | 0.00% | - |
| Stage 3 mutex | none | 3.57M | 83.88% | - |
| Stage 4 tracking | global | 0.62M | 97.22% | 1.00x |
| Stage 4 tracking | sharded | 0.64M | 97.12% | 1.03x |
| Stage 5 cache-on | global | 0.46M | 97.92% | 1.00x |
| Stage 5 cache-on | sharded | 0.57M | 97.40% | 1.24x |
| Stage 6 cache-on | global | 0.42M | 98.08% | 1.00x |
| Stage 6 cache-on | sharded | 0.53M | 97.61% | 1.25x |

四线程里，Stage 4 sharded 只有小幅提升，但在完整 cache-on Stage 5/6 下提升更明显：Stage 5 从 0.46M 到 0.57M，Stage 6 从 0.42M 到 0.53M。也就是说，sharding 对完整路径的帮助大于对纯 tracking stage 的帮助。

## 初步结论

1. sharded tracking 有收益，但不是根治 Stage 4。

   Stage 4 单线程和四线程都有小幅提升，但距离 Stage 3 仍然很远。单线程仍是 6.92M 到 1.79M，四线程仍是 3.57M 到 0.64M。说明 Stage 4 的大下降不只是 global mutex 这一个点，`unordered_set` 操作本身、地址复用模式、cache locality、malloc/free 压力都可能继续影响 tracking 路径。

2. tracking 和 writer/ring lock 分离对完整路径有价值。

   四线程 Stage 5/6 cache-on 下，sharded mode 有约 24% 到 25% 提升。这个结果说明：当 record write、thread name、ring write、notify 都存在时，把 tracking 从 writer lock 里拆出去能减少完整路径里的串行区。

3. 单线程不适合默认启用 sharded。

   单线程 Stage 5 cache-on 略慢，Stage 6 基本持平，说明 sharding 引入了额外 lock/hash/路径成本。现在它更适合作为实验开关，而不是默认优化。

4. 下一步不要只继续调 shard 数。

   更有价值的方向是拆 writer/ring mutex，或者做 per-thread tracking / per-thread buffer 这类更彻底的共享路径优化。如果只做 16/32/64/128 shard sweep，可能会陷在实现细节里，解释力不如继续拆共享串行区。

## 组会可以这样讲

这轮是把前面 tracking sub-ablation 的结论往实现优化上推进。我们做了一个 `LNHV1_TRACKING_MODE=sharded`，把单个 tracking set 拆成 64 个 shard。结果显示，Stage 4 有小幅提升，但没有把 Stage 3 到 Stage 4 的大差距抹平；在 cache-on 的完整 Stage 5/6 里，四线程提升更明显，约 24% 到 25%。这说明 tracking 的结构性问题确实存在，但瓶颈不只是“一个 global mutex”；完整路径里更关键的是 tracking 和 writer/ring 共享串行区的耦合。下一步更应该继续拆 writer/ring mutex 或 per-thread tracking，而不是只调 shard 数。
