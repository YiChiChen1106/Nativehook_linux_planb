# native_hook Plan B Stage 4 tracking sub-ablation 总结

## 这轮实验想回答什么

上一轮 producer hot-path ablation 里，Stage 3 `mutex` 到 Stage 4 `tracking` 掉得很明显：

> 单线程从约 6.90M 掉到 1.81M，四线程从约 2.75M 掉到 0.47M。

所以这轮先不拆 metadata，也不拆 record write，而是只拆 Stage 4 tracking：

> Stage 4 的新增成本更像来自 sample/filter 判断、alloc 侧 insert，还是 free 侧 lookup/erase？

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_tracking_subablation_server_2026-05-07.csv`
- workload：`perf_test_data_linux`
- allocation size：32B
- duration：5s
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- 线程数：1 线程、4 线程

这组结果只用于定位 Stage 4 tracking 的内部成本，不代表最终 native_hook overhead。

## Tracking 子 stage

| Sub-stage | 名称 | alloc 侧 | free 侧 |
|---:|---|---|---|
| 1 | `tracking_sample_filter_only` | `PassesFilter + ShouldSample` | 不做 tracking |
| 2 | `tracking_insert_only` | sample/filter + `insert(addr)` | 不做 lookup/erase |
| 3 | `tracking_lookup_only` | sample/filter + insert | `find(addr)`，不 erase |
| 4 | `tracking_full_erase` | sample/filter + insert | `find(addr) + erase` |

解释口径是“累计真实路径”：每一行都是在上一行基础上继续加一段逻辑，所以重点看相邻 stage 的差值。

## 单线程结果

| 路径 | throughput_ops | overhead vs baseline | 相邻下降 |
|---|---:|---:|---:|
| baseline no hook | 17.17M | 0.00% | - |
| Stage 3 mutex | 7.11M | 58.59% | - |
| sample/filter only | 6.25M | 63.59% | -0.86M |
| + insert | 4.08M | 76.23% | -2.17M |
| + lookup | 3.03M | 82.37% | -1.05M |
| + erase | 1.81M | 89.47% | -1.22M |

单线程里，alloc 侧 `insert` 是最明显的新增下降，free 侧 `lookup` 和 `erase` 也都不是小项。也就是说，Stage 4 的问题不是只有“要不要记录 alloc”这一个点，而是整个 `unordered_set` tracking 路径都在吃成本。

## 四线程结果

| 路径 | throughput_ops | overhead vs baseline | 相邻下降 |
|---|---:|---:|---:|
| baseline no hook | 14.10M | 0.00% | - |
| Stage 3 mutex | 2.95M | 79.10% | - |
| sample/filter only | 2.14M | 84.80% | -0.80M |
| + insert | 1.38M | 90.24% | -0.77M |
| + lookup | 1.00M | 92.94% | -0.38M |
| + erase | 0.48M | 96.60% | -0.52M |

四线程里，Stage 3 的 global mutex 已经很重。在这个基础上继续加 tracking 后，sample/filter、insert、lookup、erase 都能看到额外下降，其中 insert 和 erase 仍然比较突出。

## 初步结论

1. Stage 4 的大幅下降不是单一原因。

   sample/filter 判断本身有可见成本，但它解释不了 Stage 3 到完整 Stage 4 的全部下降。`insert`、`lookup`、`erase` 都继续带来明显吞吐损失。

2. `insert` 和 `erase` 是下一步最值得优先看的 tracking 操作。

   单线程里 `insert` 的相邻下降最大；四线程里 `insert` 和 `erase` 也都很突出。这说明当前 `unordered_set + global mutex` 的 tracking 结构可能需要继续优化。

3. 这组实验仍然是瓶颈定位，不是最终 overhead 结论。

   `tracking_insert_only` 不是完美纯微基准，因为 malloc/free workload 可能复用地址，集合大小和真实运行时分布有关。所以这里更稳妥的说法是：它估计了 alloc 侧 insert 路径的成本，而不是严格隔离出的 hash insert 单操作成本。

## 组会可以这样讲

上一轮发现 Stage 3 到 Stage 4 掉得很明显，所以这轮没有继续扫 sample/filter，也没有急着做 metadata 或 record write，而是先把 Stage 4 tracking 拆开。结果显示，tracking 的成本不是集中在一个小点上：sample/filter 有成本，alloc 侧 insert 更明显，free 侧 lookup/erase 也继续拉低吞吐。下一步如果要优化 producer hot path，tracking 数据结构和锁粒度值得优先看。

