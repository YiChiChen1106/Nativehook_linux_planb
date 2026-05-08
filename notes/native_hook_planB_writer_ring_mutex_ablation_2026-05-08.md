# native_hook Plan B writer/ring mutex sub-ablation 分叉总结

## 这轮实验想回答什么

这轮是在 tracking optimization 之后开的实验分叉，暂时不回主线。前一轮 `LNHV1_TRACKING_MODE=sharded` 显示：纯 Stage 4 tracking 只小幅提升，但 cache-on 的完整 Stage 5/6 四线程提升更明显。这个现象说明下一层问题可能不是单纯 tracking set，而是：

> tracking 已经拆出去以后，writer/ring 侧剩余串行区里还有哪些成本？

所以这轮固定使用：

- `LNHV1_TRACKING_MODE=sharded`
- `LNHV1_PID_TID_CACHE=1`
- `LNHV1_ABLATION_STAGE=5`

然后继续拆 writer mutex 内部的累计路径。

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_writer_ring_mutex_ablation_server_2026-05-08.csv`
- workload：`perf_test_data_linux`
- allocation size：32B
- duration：5s
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- 线程数：1 线程、4 线程

这组结果只作为分叉实验，不直接合并成主线结论。

## 子 stage 设计

| Sub-stage | 名称 | 新增内容 |
|---:|---|---|
| 22 | `writer_lock_only` | sharded tracking 后，只进入 writer mutex lock/unlock |
| 23 | `writer_record_fill_cached` | 在 writer lock 内填 cached pid/tid、timestamp、addr、size |
| 24 | `writer_thread_name_no_ring` | 加周期性 thread name，不写 ring |
| 25 | `writer_ring_index_check` | 加 ring capacity / read-write index / full check |
| 26 | `writer_shm_record_copy` | 加 `records_[write_index] = record` |
| 27 | `writer_atomic_index_self_drain` | 加 write/read index atomic store，接近 Stage 5 self-drain |

## 单线程结果

| 路径 | throughput_ops | overhead vs baseline | 相邻变化 |
|---|---:|---:|---:|
| baseline no hook | 17.12M | 0.00% | - |
| Stage 4 sharded tracking | 1.82M | 89.34% | - |
| Stage 5 sharded cache-on full | 1.32M | 92.31% | - |
| Stage 6 sharded cache-on full | 1.06M | 93.83% | -0.26M vs Stage 5 |
| writer lock only | 1.63M | 90.49% | -0.20M vs Stage 4 |
| + record fill cached | 1.40M | 91.83% | -0.23M |
| + thread name | 1.36M | 92.03% | -0.03M |
| + ring index check | 1.33M | 92.25% | -0.04M |
| + shm record copy | 1.31M | 92.34% | -0.01M |
| + atomic self-drain | 1.20M | 93.00% | -0.11M |

单线程里，最大的两段新增下降来自 writer mutex 本身和 cached record fill；ring index、shm copy、thread name 都是小段，atomic self-drain 有可见下降，但仍没有 pid/tid 那种大断崖。

## 四线程结果

| 路径 | throughput_ops | overhead vs baseline | 备注 |
|---|---:|---:|---|
| baseline no hook | 14.89M | 0.00% | - |
| Stage 4 sharded tracking | 0.48M | 96.78% | reference |
| Stage 5 sharded cache-on full | 0.38M | 97.42% | full producer write |
| Stage 6 sharded cache-on full | 0.37M | 97.52% | notify 差距小 |
| writer lock only | 0.46M | 96.94% | 接近 Stage 4 |
| + record fill cached | 0.44M | 97.04% | 小幅下降 |
| + thread name | 0.44M | 97.08% | 小幅下降 |
| + ring index check | 0.44M | 97.05% | 基本同量级 |
| + shm record copy | 0.41M | 97.25% | 有下降 |
| + atomic self-drain | 0.43M | 97.10% | 非单调 |

四线程里，排序不完全单调，不能过度解释每个小段的严格顺序。比较稳的是：writer lock only 已经接近 Stage 4 sharded tracking，record fill/thread name/ring index 都没有造成新的大断崖；Stage 6 full 和 Stage 5 full 差距也小，notify 仍不像当前主因。

## 初步结论

1. writer/ring 串行区没有新的单点大断崖。

   在 pid/tid cache + sharded tracking 之后，Stage 5 剩余成本更像几个小段累计，而不是某个像 pid/tid 一样的主瓶颈。

2. writer mutex 本身仍有成本，但不是唯一解释。

   单线程 writer lock only 从 Stage 4 的 1.82M 降到 1.63M；四线程从 0.48M 降到 0.46M。它有成本，但还不能解释 Stage 4 到完整 Stage 5 的全部差距。

3. record fill/cache metadata 是单线程里比较明显的一段。

   单线程从 writer lock only 到 record fill cached 是 1.63M 到 1.40M，说明即使 pid/tid 已缓存，timestamp 和 record 字段填充仍然可见。

4. 四线程下一步不宜继续细抠 ring 小段排序。

   四线程 ring/copy/atomic 有波动，说明这组 sub-stage 已经接近调度和共享路径噪声。更值得看的方向是更结构性的 per-thread tracking / per-thread writer buffer，而不是继续拆 ring 的单个 load/store。

## 组会可以这样讲

这个分叉是在 sharded tracking 之后继续问：完整 Stage 5/6 里剩下的 writer/ring 串行区是不是还有大头。结果显示，没有新的单点大断崖；writer lock、record fill、ring/copy/atomic 都有一些成本，但都不像之前 pid/tid 那样主导全局。四线程里 Stage 5 full 和 Stage 6 full 也很接近，所以 notify 仍不是当前第一优先级。这个分叉更像是在排除 writer/ring 内部某个小操作是主瓶颈；如果要继续优化，下一步应该看 per-thread tracking 或 per-thread writer buffer 这种更结构性的方案。
