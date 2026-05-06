# native_hook Plan B producer hot-path ablation 总结

## 这周实验想回答什么

前一轮 sweep 已经看到一个现象：`sample_interval` 和 `filter_size` 能明显压低 record 数量，但 throughput 很快进入平台区。

所以这周没有继续重复参数 sweep，而是把 producer 热路径拆开做 ablation，想定位剩余 overhead 更像落在哪一层：

> 是 hook entry 本身、reentry guard、mutex、allocation tracking、record 写入，还是 eventfd notify / consumer drain？

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_ablation_server_2026-05-06.csv`
- workload：`perf_test_data_linux`
- allocation size：32B
- duration：5s
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- 线程数：1 线程、4 线程

这组实验是 record 密集场景，目的是放大 producer hot path 的成本，不代表优化参数下的最终吞吐。

## 6 个 ablation stage

| Stage | 名称 | 保留的热路径 |
|---:|---|---|
| 1 | hook_entry | 只进入 `malloc/free` hook wrapper，然后直接返回 |
| 2 | guard | Stage 1 + `HookReentryGuard` 线程本地重入保护 |
| 3 | mutex | Stage 2 + `HookWriter` 入口 + `pthread_mutex` lock/unlock |
| 4 | tracking | Stage 3 + allocation tracking，记录 kept alloc，free 只跟踪已记录 alloc |
| 5 | record_write | Stage 4 + metadata 填充 + shared memory record write，但不做 eventfd notify |
| 6 | notify | 当前完整路径：record write + eventfd notify + consumer batch drain |

说明：Stage 5 为了只测 producer record write，没有通知 consumer；producer 侧会做 ablation-only 的自 drain，避免 ring buffer 填满后把结果变成 dropped 测试。

## 结果表

### 单线程

| Stage | 名称 | throughput_ops | overhead vs baseline |
|---:|---|---:|---:|
| baseline | no hook | 17.26M | 0.00% |
| 1 | hook_entry | 14.91M | 13.62% |
| 2 | guard | 10.14M | 41.25% |
| 3 | mutex | 6.90M | 60.04% |
| 4 | tracking | 1.81M | 89.54% |
| 5 | record_write | 0.34M | 98.01% |
| 6 | notify | 0.31M | 98.23% |

Stage 6 consumer metrics：

- records：3,061,545
- alloc：1,529,246
- free：1,529,242
- thread_name：3,057
- flush：153,049
- dropped：0
- avg_batch：20.00

### 四线程

| Stage | 名称 | throughput_ops | overhead vs baseline |
|---:|---|---:|---:|
| baseline | no hook | 12.89M | 0.00% |
| 1 | hook_entry | 12.93M | -0.37% |
| 2 | guard | 10.59M | 17.80% |
| 3 | mutex | 2.75M | 78.66% |
| 4 | tracking | 0.47M | 96.33% |
| 5 | record_write | 0.08M | 99.39% |
| 6 | notify | 0.07M | 99.43% |

Stage 6 consumer metrics：

- records：730,440
- alloc：364,859
- free：364,849
- thread_name：732
- flush：36,522
- dropped：0
- avg_batch：20.00

## 初步结论

1. notify 不是这组实验里的最大新增成本。

Stage 5 到 Stage 6 的差距很小。单线程从 0.34M 到 0.31M，四线程从 0.08M 到 0.07M。说明在 record 密集场景里，`eventfd notify + consumer batch drain` 不是最先看到的大头。

2. producer 本地 record write 之前已经很重。

Stage 4 到 Stage 5 掉得最明显之一。这里增加的是 metadata 填充、时间戳、pid/tid、shared memory ring 写入和原子 index 更新。这个结果支持之前的判断：即使 consumer 端 batching 正常，producer 本地热路径仍然会吃掉大量吞吐。

3. allocation tracking 的成本也很突出。

Stage 3 到 Stage 4 单线程从 6.90M 掉到 1.81M，四线程从 2.75M 掉到 0.47M。当前 tracking 用的是全局 mutex 下的 `unordered_set`，这可能比真实系统里更粗糙，但它清楚说明：如果 free 只跟随 kept alloc，tracking 数据结构本身也需要认真优化。

4. 多线程下 mutex 影响被明显放大。

四线程 Stage 2 到 Stage 3 从 10.59M 掉到 2.75M，说明全局 mutex 很快会变成 producer hot path 的瓶颈。后续如果要更接近真实优化方向，可能需要考虑 per-thread buffer、锁分片，或者把 sample/filter 尽量前移到锁外。

## 组会上可以这样讲

这一页的核心不是说 native_hook 一定慢，而是说：在 Plan B 的 record 密集压力测试里，剩余 overhead 已经不主要像是 consumer drain 的问题，而更像是 producer 本地热路径的问题。

可以按这个顺序讲：

1. 之前 sweep 发现 sample/filter 能降 record，但 throughput 有平台区。
2. 所以这周把 producer 热路径拆成 6 层。
3. 结果显示 notify 的增量相对小，tracking、mutex、record write 更值得继续拆。
4. 下一步不急着补 stack unwind，先把 producer 本地路径继续细分：tracking 数据结构、timestamp/pid/tid、ring write、锁粒度。

## 想请老师判断的问题

- 当前 Plan B 的 `unordered_set + global mutex` tracking 是否已经足够说明“tracking 是大头之一”，还是需要换成更贴近 upstream 的结构后再下结论？
- 下一轮是否应该继续做更细的 producer sub-ablation，例如：
  - tracking only：hash insert/erase 拆开
  - metadata only：`clock_gettime`、`getpid`、`gettid` 分开
  - ring only：只测 shared memory 写和 index 原子更新
  - lock only：global mutex vs per-thread / sharded lock
- 在汇报时，是把这组结果定位成“性能瓶颈定位实验”，而不是“最终 overhead 数字”，这样是否更稳妥？

