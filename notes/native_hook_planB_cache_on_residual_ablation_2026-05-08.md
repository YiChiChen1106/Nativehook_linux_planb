# native_hook Plan B cache-on residual ablation 总结

## 这轮实验想回答什么

上一轮已经证明 `LNHV1_PID_TID_CACHE=1` 是有效优化：Stage 5/6 吞吐能提升约 3 到 4 倍。今天这轮不再拆 pid/tid，而是基于 cache-on 路径继续看剩余成本：

> pid/tid cache 之后，Stage 4 tracking 到 Stage 5 cache-on 的剩余差距，主要来自 thread name、ring index、shared memory copy、atomic self-drain，还是 Stage 6 notify/consumer？

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_cache_on_residual_ablation_server_2026-05-08.csv`
- workload：`perf_test_data_linux`
- allocation size：32B
- duration：5s
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- 线程数：1 线程、4 线程

这组实验仍然是 producer hot-path 定位实验，不代表真实应用最终 overhead。

## 子 stage 设计

| Sub-stage | 名称 | 含义 |
|---:|---|---|
| 17 | `metadata_cached_pid_thread_local_tid` | full tracking + record fill + clock + cached pid/tid，不写 ring |
| 18 | `cached_thread_name_no_ring` | 在 17 基础上加周期性 thread name，仍不写 ring |
| 19 | `cached_ring_index_check` | 在 18 基础上加 ring capacity / read-write index / full check |
| 20 | `cached_shm_record_copy` | 在 19 基础上加 `records_[write_index] = record` |
| 21 | `cached_atomic_index_self_drain` | 在 20 基础上加 write/read index atomic store，接近 Stage 5 cache-on self-drain |

`18..21` 强制使用 cached pid + thread-local tid，不依赖全局 `LNHV1_PID_TID_CACHE`。

## 单线程结果

| 路径 | throughput_ops | overhead vs baseline | 相邻变化 |
|---|---:|---:|---:|
| baseline no hook | 16.91M | 0.00% | - |
| Stage 4 tracking | 1.67M | 90.13% | - |
| Stage 5 cache off | 0.33M | 98.02% | - |
| Stage 5 cache on | 1.37M | 91.92% | +1.03M vs off |
| Stage 6 cache on | 1.08M | 93.63% | -0.29M vs Stage 5 cache on |
| sub17 cached pid/tid | 1.37M | 91.87% | reference |
| sub18 + thread name | 1.34M | 92.06% | -0.03M |
| sub19 + ring index check | 1.32M | 92.20% | -0.02M |
| sub20 + shm copy | 1.29M | 92.38% | -0.03M |
| sub21 + atomic self-drain | 1.25M | 92.61% | -0.04M |

单线程里，cache-on 后的 Stage 5 剩余成本不是一个新的单点崩塌。thread name、ring index、shm copy、atomic self-drain 都有小幅成本，但没有任何一段像之前 pid/tid 那样造成数量级下降。

更明显的是 Stage 5 cache-on 到 Stage 6 cache-on 仍有约 0.29M 的下降，所以 notify/consumer 方向值得下一轮单独拆。

## 四线程结果

| 路径 | throughput_ops | overhead vs baseline | 备注 |
|---|---:|---:|---|
| baseline no hook | 13.66M | 0.00% | - |
| Stage 4 tracking | 0.46M | 96.60% | - |
| Stage 5 cache off | 0.07M | 99.45% | - |
| Stage 5 cache on | 0.28M | 97.98% | cache 仍有效 |
| Stage 6 cache on | 0.27M | 98.00% | 与 Stage 5 cache-on 接近 |
| sub17 cached pid/tid | 0.43M | 96.86% | 接近 Stage 4 |
| sub18 + thread name | 0.46M | 96.64% | 与 sub17 基本同量级 |
| sub19 + ring index check | 0.40M | 97.10% | 有下降 |
| sub20 + shm copy | 0.35M | 97.46% | 有下降 |
| sub21 + atomic self-drain | 0.43M | 96.83% | 反而回升 |

四线程里排序不够单调，不能把细小差异解释成严格的微基准。比较稳妥的结论是：cache-on 后 Stage 5/6 仍然受多线程共享路径影响，但这轮结果不能证明 ring index、shm copy 或 atomic self-drain 是新的主瓶颈。

另外，四线程 Stage 5 cache-on 和 Stage 6 cache-on 几乎同量级，说明 notify 在四线程下不是主要新增下降；更值得看的可能是完整路径里的锁、tracking 容器、配置开关检查和分支形态。

## 初步结论

1. pid/tid cache 后，Stage 5 剩余 producer 成本变成“分散小成本”。

   单线程从 sub17 到 sub21 是渐进下降，没有新的大断崖。ring/copy/atomic 都有成本，但不是像 pid/tid 那样的最大头。

2. 单线程下一步更应该拆 Stage 6 notify/consumer。

   Stage 5 cache-on 是 1.37M，Stage 6 cache-on 是 1.08M，差距比单个 ring 子段更明显。下一轮可以拆 eventfd write、flush threshold、consumer wakeup/drain。

3. 四线程下一步要回到锁和共享数据结构。

   四线程下 Stage 4 tracking 只有 0.46M，Stage 5/6 cache-on 约 0.27M；吞吐主要被共享 hot path 放大限制。这里不宜继续在单个 record 字段上抠太细。

4. `21` 与 Stage 5 full cache-on 的关系要保守解释。

   单线程同量级，四线程不完全贴合。这说明 sub-stage 是定位工具，不是完整路径的完美替身。组会里可以说“它帮助排除 ring/atomic 是单点主因”，不要说“它完全等价完整 Stage 5”。

## 组会可以这样讲

昨天我们把 pid/tid cache 做成正式开关，今天基于 cache-on 路径继续拆剩余成本。单线程结果显示，thread name、ring index、shm copy、atomic self-drain 都只带来小幅下降，没有新的数量级瓶颈；Stage 5 cache-on 到 Stage 6 cache-on 的差距反而更值得看。四线程结果更不单调，说明共享路径和锁竞争的影响已经大于这些细粒度 record write 差异。下一步我建议拆 Stage 6 notify/consumer，同时另开一条线看四线程下 mutex/tracking 容器。
