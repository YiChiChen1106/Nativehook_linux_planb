# native_hook Plan B Stage 5 pid/tid metadata sub-ablation 总结

## 这轮实验想回答什么

上一轮 Stage 5 `record_write` sub-ablation 显示，最大新增下降发生在 `metadata_pid_tid`：单线程从 `metadata_clock` 的约 1.53M 掉到 0.33M，四线程从约 0.46M 掉到 0.09M。

所以这轮只继续拆 pid/tid metadata，不继续拆 ring，不做 eBPF，也不把这组结果说成最终 native_hook overhead。目标是判断这段下降主要来自 `getpid()`、`syscall(SYS_gettid)`，还是热路径上重复取 pid/tid 这件事本身。

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_pid_tid_subablation_server_2026-05-07.csv`
- workload：`perf_test_data_linux`
- allocation size：32B
- duration：5s
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- 线程数：1 线程、4 线程

这组实验仍然是 record 密集场景下的 producer hot-path 定位实验。

## 子 stage 设计

| Sub-stage | 名称 | 含义 |
|---:|---|---|
| 6 | `metadata_clock` | Stage 5 参考点：full tracking + record fill + `clock_gettime` |
| 7 | `metadata_pid_tid` | 旧的 pid/tid 控制组 |
| 12 | `metadata_pid_only` | 只加 `getpid()` |
| 13 | `metadata_tid_syscall_only` | 只加 `syscall(SYS_gettid)` |
| 14 | `metadata_pid_tid_syscall` | 加 `getpid()` + `syscall(SYS_gettid)`，复现旧控制组 |
| 15 | `metadata_cached_pid_only` | 使用进程级 cached pid |
| 16 | `metadata_thread_local_tid_only` | 使用 thread-local cached tid |
| 17 | `metadata_cached_pid_thread_local_tid` | 同时使用 cached pid + thread-local tid |

`12..17` 只包含 full tracking、record fill、clock 和对应 pid/tid metadata，不做 thread name、ring index、shared memory copy 或 atomic index update。

## 单线程结果

| 路径 | throughput_ops | overhead vs baseline | 相邻/参考变化 |
|---|---:|---:|---:|
| baseline no hook | 16.58M | 0.00% | - |
| Stage 4 full tracking | 1.77M | 89.34% | - |
| metadata_clock | 1.51M | 90.88% | 参考点 |
| metadata_pid_only | 0.56M | 96.61% | 比 clock 低约 0.95M |
| metadata_tid_syscall_only | 0.57M | 96.56% | 比 clock 低约 0.94M |
| metadata_pid_tid_syscall | 0.35M | 97.89% | 接近旧 pid/tid |
| old metadata_pid_tid | 0.35M | 97.91% | 控制组 |
| Stage 5 full control | 0.35M | 97.92% | 同量级 |
| cached_pid_only | 1.55M | 90.67% | 回到 clock 附近 |
| thread_local_tid_only | 1.53M | 90.80% | 回到 clock 附近 |
| cached_pid_thread_local_tid | 1.52M | 90.82% | 回到 clock 附近 |

单线程里，`getpid()` 和 `syscall(SYS_gettid)` 单独加入都会把吞吐从 1.51M 拉到约 0.56M；二者一起加入后降到 0.35M，基本复现旧 `metadata_pid_tid` 和完整 Stage 5。

## 四线程结果

| 路径 | throughput_ops | overhead vs baseline | 相邻/参考变化 |
|---|---:|---:|---:|
| baseline no hook | 14.57M | 0.00% | - |
| Stage 4 full tracking | 0.51M | 96.51% | - |
| metadata_clock | 0.46M | 96.87% | 参考点 |
| metadata_pid_only | 0.15M | 98.97% | 比 clock 低约 0.31M |
| metadata_tid_syscall_only | 0.12M | 99.14% | 比 clock 低约 0.33M |
| metadata_pid_tid_syscall | 0.08M | 99.42% | 接近旧 pid/tid / full control |
| old metadata_pid_tid | 0.07M | 99.50% | 控制组 |
| Stage 5 full control | 0.09M | 99.38% | 同量级 |
| cached_pid_only | 0.45M | 96.93% | 回到 clock 附近 |
| thread_local_tid_only | 0.44M | 96.97% | 回到 clock 附近 |
| cached_pid_thread_local_tid | 0.44M | 96.95% | 回到 clock 附近 |

四线程趋势一致：pid-only 和 tid-only 都显著低于 clock reference；cached 版本基本回到 `metadata_clock` 附近。

## 初步结论

1. Stage 5 最大瓶颈不是 ring write，而是每条 record 里重复获取 pid/tid。

   `metadata_pid_tid_syscall` 能复现旧 `metadata_pid_tid`，并且接近完整 Stage 5。说明上一轮看到的大幅下降主要由 pid/tid metadata fetch 触发。

2. 不能把责任只压到 `gettid` 上。

   `syscall(SYS_gettid)` 很可疑，但这轮里 `metadata_pid_only` 也出现了同量级下降。因此更稳妥的表述是：在 record 密集路径里，逐条调用 `getpid()` 和 `syscall(SYS_gettid)` 都很贵，二者叠加后基本解释 Stage 5 的主要下降。

3. cache 对照组很关键。

   cached pid、thread-local tid、cached pid + thread-local tid 都回到 `metadata_clock` 附近，说明如果后续要优化 Stage 5，优先方向应该是把 pid/tid 从 per-record syscall/函数调用路径移出去。

4. cached sub-stage 仍然只是 ablation-only 对照。

   这轮只证明“缓存方向值得做”，还不能直接说最终实现已经完成。下一步应该做一个正式优化开关或实现分支，再重新跑 Stage 5/Stage 6 end-to-end。

## 组会可以这样讲

上一轮 Stage 5 里 `metadata_pid_tid` 一加入，吞吐就掉到完整 Stage 5 的量级。所以这轮我把 pid/tid 拆开看。结果是：`getpid()` 单独加和 `syscall(SYS_gettid)` 单独加都很重，二者一起基本复现完整 Stage 5；而 cached pid 和 thread-local tid 基本回到 `clock_gettime` 参考点附近。因此下一步不应该先优化 ring，而应该先把 pid/tid 从每条 record 的热路径里缓存掉，然后再重新跑完整 Stage 5/Stage 6。
