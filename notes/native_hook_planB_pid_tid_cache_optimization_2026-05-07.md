# native_hook Plan B pid/tid cache 正式优化开关总结

## 这轮做了什么

上一轮 pid/tid sub-ablation 证明：Stage 5 最大下降主要来自每条 record 热路径里重复取 `getpid()` 和 `syscall(SYS_gettid)`。这轮把这个发现落成一个正式优化开关：

- 开关：`LNHV1_PID_TID_CACHE=1`
- 默认：不开启，保持原有行为
- 生效范围：完整 Stage 5 `record_write` 和 Stage 6 `notify`
- 不影响：`LNHV1_SUBABLATION_STAGE=12..17` 的诊断语义

开启后，record metadata 使用进程级 cached pid 和 thread-local cached tid。实现里加了 `pthread_atfork` child handler，避免 fork 后沿用父进程 pid/tid cache。

## 实验设置

- 机器：服务器 `pink`
- 代码目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_pid_tid_cache_optimization_server_2026-05-07.csv`
- workload：`perf_test_data_linux`
- allocation size：32B
- duration：5s
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- 线程数：1 线程、4 线程

这组实验是 producer hot-path 优化验证，不代表最终 native_hook 在真实应用上的开销。

## 单线程结果

| 路径 | cache | throughput_ops | overhead vs baseline | 相对 cache off |
|---|---:|---:|---:|---:|
| baseline no hook | - | 17.15M | 0.00% | - |
| Stage 4 tracking | - | 1.69M | 90.12% | - |
| Stage 5 record_write | off | 0.34M | 98.03% | 1.00x |
| Stage 5 record_write | on | 1.33M | 92.23% | 3.94x |
| Stage 6 notify | off | 0.30M | 98.23% | 1.00x |
| Stage 6 notify | on | 1.09M | 93.66% | 3.58x |

单线程里，pid/tid cache 把完整 Stage 5 从 0.34M 提到 1.33M，把完整 Stage 6 从 0.30M 提到 1.09M。它没有完全回到 Stage 4 tracking 的 1.69M，说明 record fill、clock、ring/notify 仍然有剩余成本，但最大的一段已经被明显削掉。

## 四线程结果

| 路径 | cache | throughput_ops | overhead vs baseline | 相对 cache off |
|---|---:|---:|---:|---:|
| baseline no hook | - | 13.63M | 0.00% | - |
| Stage 4 tracking | - | 0.48M | 96.48% | - |
| Stage 5 record_write | off | 0.09M | 99.37% | 1.00x |
| Stage 5 record_write | on | 0.31M | 97.75% | 3.58x |
| Stage 6 notify | off | 0.07M | 99.46% | 1.00x |
| Stage 6 notify | on | 0.24M | 98.27% | 3.20x |

四线程里也有同样趋势：cache on 后 Stage 5/6 都有约 3 倍以上提升。不过四线程仍明显低于 Stage 4，说明锁竞争、tracking 容器和 notify 之后的共享路径仍然会限制扩展性。

## 初步结论

1. pid/tid cache 是当前最明确、收益最大的 Stage 5 优化。

   它直接把上一轮定位到的 `metadata_pid_tid` 热点从完整路径中移走，Stage 5 和 Stage 6 都得到 3x 以上吞吐提升。

2. 默认关闭是合适的。

   这个开关改变 metadata 获取策略；虽然实现加了 fork child cache reset，但作为优化验证阶段，默认保持旧语义更稳妥。汇报时可以说它是“正式优化开关”，不是默认启用策略。

3. 下一步不应该再继续拆 pid/tid。

   pid/tid 这段已经从“定位”进入“可优化”。后续更值得看 cache on 后剩下的差距：Stage 4 tracking 到 Stage 5 cache-on 之间的 record fill/clock/ring，或者四线程下的锁和 tracking 容器扩展性。

## 组会可以这样讲

上一轮我们发现 `getpid/gettid` 基本解释了 Stage 5 的主要下降。所以这轮我做了一个正式开关 `LNHV1_PID_TID_CACHE=1`，让完整 Stage 5/6 使用 cached pid 和 thread-local tid。服务器结果显示，单线程 Stage 5 从 0.34M 提到 1.33M，Stage 6 从 0.30M 提到 1.09M；四线程也有 3 倍左右提升。这说明 pid/tid cache 是目前最明确的一步优化。接下来应该基于 cache-on 的路径继续看剩余成本，而不是再停留在 pid/tid 拆分上。
