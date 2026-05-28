# native_hook libbpf fixed-work comparison

## 实验目的

这组实验把 eBPF/libbpf 对比也改成固定工作量口径：

> 固定做 100 万次 malloc/free pair，LD_PRELOAD 和 libbpf/uProbe 各自需要多少秒？

这次没有开启 `LNHV1_HOTPATH_PROFILE`，因此它比 profile fixed-work 更适合做 eBPF 和 LD_PRELOAD 的直接完成时间对比。

## 实验设置

- 服务器：`pink`
- 目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/libbpf_fixed_work_comparison_server_2026-05-27.csv`
- 本地副本：`F:\codex_workspace\native_hook\planb_project_page\linux_native_hook_v1\results\libbpf_fixed_work_comparison_server_2026-05-27.csv`
- workload：`perf_test_data_linux --iters-per-thread`
- 固定总工作量：`1000000` 次 malloc/free pair
- `threads=1`：每线程 `1000000` 次
- `threads=4`：每线程 `250000` 次

## 固定工作量结果

| Mode | 1T elapsed | 4T elapsed | 说明 |
|---|---:|---:|---|
| baseline no hook | 0.018s | 0.044s | 无观测路径 |
| LD_PRELOAD default | 3.801s | 18.556s | global tracking + cache off |
| LD_PRELOAD optimized | 1.512s | 2.947s | thread_local_fallback + cache on |
| libbpf count_only | 6.272s | 1.904s | 最小 uProbe tax |
| libbpf sample_filter | 6.527s | 1.950s | 加 sample/filter |
| libbpf tracking | 6.419s | 1.967s | 加 BPF map tracking |
| libbpf ring_output | 8.114s | 2.505s | 加 BPF ringbuf 输出 |

相对 `LD_PRELOAD optimized`：

| Mode | 1T 差值 | 4T 差值 |
|---|---:|---:|
| libbpf count_only | 慢 4.760s | 快 1.043s |
| libbpf sample_filter | 慢 5.016s | 快 0.997s |
| libbpf tracking | 慢 4.907s | 快 0.980s |
| libbpf ring_output | 慢 6.602s | 快 0.442s |

## 解释口径

- 单线程下，libbpf/uProbe 明显慢于 optimized LD_PRELOAD，说明 uProbe 本身 tax 很重。
- 四线程下，libbpf 反而快于 optimized LD_PRELOAD；其中 ring_output `2.505s`，optimized LD_PRELOAD `2.947s`，快 `0.442s`。
- 这不是说 eBPF 单次 hook 更轻，而是说明进程外观测绕开了当前进程内的一部分共享锁和 writer/ring 路径。
- 这组仍只覆盖 malloc/free，不包含 stack unwind，也不是完整 trace backend。

## 组会讲法

可以这样讲：

> 我把 eBPF 也改成固定工作量口径。固定 100 万次 malloc/free pair，单线程下 libbpf ringbuf 要 8.11 秒，比 optimized LD_PRELOAD 的 1.51 秒慢很多；但四线程下 libbpf ringbuf 是 2.50 秒，optimized LD_PRELOAD 是 2.95 秒，eBPF 快约 0.44 秒。所以 eBPF 的价值不是单次 uProbe 更轻，而是在多线程时绕开了进程内共享路径的一部分成本。
