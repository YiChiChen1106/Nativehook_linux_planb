# native_hook Plan B per-thread tracking 总结

## 这轮实验想回答什么

上一轮 sharded tracking 说明：单纯把 global tracking set 分成 64 个 shard 有收益，但不能根治 Stage 4；在完整 Stage 5/6 里收益更明显，说明 tracking 和 writer/ring 串行区耦合仍然重要。

这轮继续往结构性 per-thread 方向走，但不碰 per-thread ring/buffer，也不改 consumer 语义。目标是回答：

> 如果 tracking 尽量线程本地化，Stage 4 和 cache-on Stage 5/6 能不能继续提升？跨线程 free fallback 会不会吃掉收益？

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_per_thread_tracking_server_2026-05-08.csv`
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

这轮扩展 `LNHV1_TRACKING_MODE`：

- `global`：单个 global mutex + 单个 unordered_set
- `sharded`：64 个 tracking shard
- `thread_local_fallback`：每线程 local set，同时维护 sharded fallback mirror，用来处理跨线程 free
- `thread_local_only`：只查当前线程 local set，不处理跨线程 free，只作为性能上界

另外增加了 cross-thread free smoke workload：一个线程 alloc，另一个线程 free。语义 smoke 显示：

- `thread_local_fallback` 能记录 cross-thread free：约 10004 alloc / 9999 free
- `thread_local_only` 不能记录 cross-thread free：约 9994 alloc / 0 free

所以 `thread_local_only` 只能作为上界对照，不能当作生产语义。

## 单线程结果

| 路径 | tracking mode | throughput_ops | overhead vs baseline | 相对 global |
|---|---|---:|---:|---:|
| baseline no hook | none | 17.20M | 0.00% | - |
| Stage 3 mutex | none | 7.06M | 58.97% | - |
| Stage 4 tracking | global | 1.76M | 89.75% | 1.00x |
| Stage 4 tracking | sharded | 1.86M | 89.16% | 1.06x |
| Stage 4 tracking | thread_local_fallback | 1.11M | 93.53% | 0.63x |
| Stage 4 tracking | thread_local_only | 1.95M | 88.65% | 1.11x |
| Stage 5 cache-on | global | 1.37M | 92.02% | 1.00x |
| Stage 5 cache-on | sharded | 1.35M | 92.17% | 0.98x |
| Stage 5 cache-on | thread_local_fallback | 0.90M | 94.74% | 0.66x |
| Stage 5 cache-on | thread_local_only | 1.40M | 91.86% | 1.02x |
| Stage 6 cache-on | global | 1.07M | 93.78% | 1.00x |
| Stage 6 cache-on | sharded | 1.06M | 93.84% | 0.99x |
| Stage 6 cache-on | thread_local_fallback | 0.75M | 95.61% | 0.70x |
| Stage 6 cache-on | thread_local_only | 1.09M | 93.64% | 1.02x |

单线程里，`thread_local_only` 只比 global 略好，`thread_local_fallback` 明显更慢。原因是 fallback 模式为了保留跨线程 free 语义，每个 alloc/free 仍要维护 sharded mirror，等于 local set 和 fallback set 双写/双查。

## 四线程结果

| 路径 | tracking mode | throughput_ops | overhead vs baseline | 相对 global |
|---|---|---:|---:|---:|
| baseline no hook | none | 13.95M | 0.00% | - |
| Stage 3 mutex | none | 3.16M | 77.37% | - |
| Stage 4 tracking | global | 0.62M | 95.56% | 1.00x |
| Stage 4 tracking | sharded | 0.51M | 96.35% | 0.82x |
| Stage 4 tracking | thread_local_fallback | 0.46M | 96.70% | 0.74x |
| Stage 4 tracking | thread_local_only | 5.51M | 60.46% | 8.90x |
| Stage 5 cache-on | global | 0.35M | 97.48% | 1.00x |
| Stage 5 cache-on | sharded | 0.40M | 97.11% | 1.15x |
| Stage 5 cache-on | thread_local_fallback | 0.37M | 97.31% | 1.07x |
| Stage 5 cache-on | thread_local_only | 0.45M | 96.78% | 1.28x |
| Stage 6 cache-on | global | 0.27M | 98.09% | 1.00x |
| Stage 6 cache-on | sharded | 0.37M | 97.33% | 1.40x |
| Stage 6 cache-on | thread_local_fallback | 0.36M | 97.45% | 1.33x |
| Stage 6 cache-on | thread_local_only | 0.42M | 96.98% | 1.58x |

四线程里，`thread_local_only` 在纯 Stage 4 上提升非常大，说明如果完全避免共享 tracking 结构，tracking 本身确实能接近 Stage 3 之前的水平。但完整 Stage 5/6 仍被 writer/ring 路径限制，所以 Stage 5/6 的提升没有 Stage 4 那么大。

## 初步结论

1. per-thread tracking 的性能上界很高。

   四线程 Stage 4 从 global 的 0.62M 到 `thread_local_only` 的 5.51M，说明当前 Stage 4 的大头确实来自共享 tracking 结构和锁竞争。

2. 朴素 fallback mirror 会吃掉收益。

   `thread_local_fallback` 为了保留跨线程 free，每个 alloc/free 同时维护 thread-local set 和 sharded fallback mirror，结果 Stage 4 反而低于 global/sharded。这个路径语义更完整，但不是一个好的最终优化。

3. 完整 Stage 5/6 的上限仍然被 writer/ring 限制。

   即使使用 `thread_local_only`，四线程 Stage 6 也只是从 0.27M 到 0.42M，没有像 Stage 4 那样数量级提升。说明 tracking 优化之外，writer/ring 共享串行区仍然会压住完整路径。

4. 下一步不应该默认启用 thread-local only。

   它不处理跨线程 free，只能说明“如果 free 基本发生在同线程，per-thread tracking 很有潜力”。要变成可用优化，需要设计低成本 ownership/fallback 机制，而不是简单维护一份 sharded mirror。

## 组会可以这样讲

这轮我继续往 per-thread tracking 方向试了一步。结果很有区分度：`thread_local_only` 在四线程 Stage 4 能从 0.62M 提到 5.51M，说明 tracking 的共享结构确实是大瓶颈；但它不处理跨线程 free。为了保留语义，我做了 `thread_local_fallback`，每个 alloc 同时写 local set 和 sharded fallback mirror，结果反而变慢，说明朴素 fallback 会吃掉 per-thread 的收益。结论是：per-thread tracking 方向值得继续，但不能用简单 mirror 作为最终方案；下一步要么设计更低成本的 ownership/fallback，要么继续拆 writer/ring，因为完整 Stage 5/6 仍然被共享 writer 路径限制。
