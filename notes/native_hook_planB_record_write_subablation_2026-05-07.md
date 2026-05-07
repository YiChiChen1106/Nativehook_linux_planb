# native_hook Plan B Stage 5 record_write sub-ablation 总结

## 这轮实验想回答什么

上一轮已经把 Stage 4 tracking 拆开，发现 tracking 内部不是单点问题：sample/filter、insert、lookup、erase 都有成本。

但 Stage 4 到 Stage 5 仍然有一个更大的未解释下降：

> 单线程约从 1.81M 掉到 0.34M，四线程约从 0.47M 掉到 0.08M。

所以这轮不继续深挖 Stage 4，而是拆 Stage 5 `record_write`，想判断新增成本主要来自 metadata，还是 shared memory ring write / atomic index update。

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_record_write_subablation_server_2026-05-07.csv`
- workload：`perf_test_data_linux`
- allocation size：32B
- duration：5s
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- 线程数：1 线程、4 线程

这组实验仍然是 record 密集场景下的瓶颈定位，不代表最终 native_hook overhead。

## Stage 5 子 stage

| Sub-stage | 名称 | 新增内容 |
|---:|---|---|
| 5 | `record_fill_minimal` | full tracking 后，只填 `type/addr/size` 等最小字段 |
| 6 | `metadata_clock` | 加 `clock_gettime` |
| 7 | `metadata_pid_tid` | 加 `getpid/gettid` |
| 8 | `metadata_thread_name` | 加周期性 thread name 采集，含 `prctl(PR_GET_NAME)` |
| 9 | `ring_index_check` | 加 ring capacity、read/write index load、full check |
| 10 | `shm_record_copy` | 加 `records_[write_index] = record` |
| 11 | `atomic_index_self_drain` | 加 write/read index atomic store，接近 Stage 5 self-drain 写入路径 |

解释口径是“累计真实路径”：每一行都在上一行基础上继续加一段逻辑，所以重点看相邻 stage 的差值。

## 单线程结果

| 路径 | throughput_ops | overhead vs baseline | 相邻变化 |
|---|---:|---:|---:|
| baseline no hook | 16.36M | 0.00% | - |
| Stage 4 full tracking | 1.71M | 89.56% | - |
| record fill minimal | 1.61M | 90.17% | -0.10M |
| + clock_gettime | 1.53M | 90.62% | -0.07M |
| + getpid/gettid | 0.33M | 97.97% | -1.20M |
| + thread name | 0.33M | 97.97% | 约持平 |
| + ring index check | 0.33M | 97.98% | 约持平 |
| + shm record copy | 0.33M | 97.96% | 约持平 |
| + atomic self-drain | 0.33M | 98.01% | 约持平 |
| Stage 5 full control | 0.33M | 97.98% | 同量级 |

单线程里，最明显的下降发生在 `metadata_pid_tid`：从 1.53M 掉到 0.33M。后面的 thread name、ring index、record copy、atomic self-drain 都没有再带来同等级下降。

## 四线程结果

| 路径 | throughput_ops | overhead vs baseline | 相邻变化 |
|---|---:|---:|---:|
| baseline no hook | 13.82M | 0.00% | - |
| Stage 4 full tracking | 0.48M | 96.53% | - |
| record fill minimal | 0.46M | 96.67% | -0.02M |
| + clock_gettime | 0.46M | 96.64% | 约持平 |
| + getpid/gettid | 0.09M | 99.31% | -0.37M |
| + thread name | 0.08M | 99.44% | -0.02M |
| + ring index check | 0.07M | 99.49% | -0.01M |
| + shm record copy | 0.07M | 99.47% | 约持平 |
| + atomic self-drain | 0.09M | 99.34% | 同量级 |
| Stage 5 full control | 0.07M | 99.53% | 同量级 |

四线程里也能看到同样趋势：`metadata_pid_tid` 是最大新增下降。后半段 ring/copy/atomic 的差值不稳定，说明它们在这组实验里已经接近测量波动和调度噪声，不能过度解释细小排序。

## 初步结论

1. Stage 5 的最大嫌疑不是 shared memory copy，也不是 atomic index update。

   单线程和四线程都显示，加入 `getpid/gettid` 后吞吐直接接近完整 Stage 5。后面的 ring index、record copy、atomic self-drain 没有再造成同等级下降。

2. `getpid/gettid` 需要继续拆。

   当前 sub-stage 把 `getpid` 和 `gettid` 放在一起。考虑到 `gettid` 走 `syscall(SYS_gettid)`，下一轮更应该拆成：
   - pid only
   - tid only
   - pid/tid cached
   - thread-local tid cache

3. `clock_gettime` 有成本，但不是这轮最大头。

   单线程从 1.61M 到 1.53M，有可见下降；四线程基本持平。它值得记录，但优先级低于 pid/tid。

4. Stage 5 self-drain 只是 ablation-only 行为。

   这轮 Stage 5 仍然不代表最终 consumer drain 语义；它只是为了在不 notify consumer 的情况下避免 ring buffer 填满。汇报时要把它讲成 producer-side record write 定位实验。

## 组会可以这样讲

上一轮我们发现 Stage 4 tracking 已经很重，但 Stage 4 到 Stage 5 还有更大下降。所以这轮把 Stage 5 拆成 metadata 和 ring write 两部分。结果最清楚的一点是：`getpid/gettid` 一加入，吞吐就基本掉到完整 Stage 5 的量级；而 shared memory copy 和 atomic index update 没有表现出同样大的新增成本。下一步不应该先优化 ring，而应该先把 pid/tid 拆开，看是不是 `syscall(SYS_gettid)` 在 record 密集场景里成为主要瓶颈。

