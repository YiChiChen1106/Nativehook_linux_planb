# native_hook libbpf/uProbe 对比实验说明

## 这轮升级回答什么

这轮把 eBPF 对比从 bpftrace 原型升级成 libbpf loader。目标不是立刻替代 native_hook，而是得到一个更可控的外部观测基线：目标进程过滤、结构化计数、真实 BPF ringbuf 输出。

## 实现内容

- 保留 `linux_native_hook_v1/ebpf_probe/*.bt.in`，继续作为 bpftrace 原型。
- 新增 `uprobe_probe.bpf.c` 和 `uprobe_loader.cpp`：
  - loader fork workload，并把 child pid 写入 BPF config。
  - BPF 程序只统计目标 tgid，减少 bpftrace 版本里的额外计数噪声。
  - `libbpf_ring_output` 使用 `BPF_MAP_TYPE_RINGBUF`，不再用 bpftrace `printf` 近似。
- 新增 `scripts/run_libbpf_uprobe_comparison.sh`，输出 baseline、LD_PRELOAD Stage 6 default/optimized，以及 libbpf 四个模式。

## 模式设计

| mode | 含义 |
|---|---|
| `libbpf_count_only` | 只统计 malloc/free 次数，测最小 uProbe tax |
| `libbpf_sample_filter` | 加 sample/filter 和 malloc return |
| `libbpf_tracking` | 加 `(tgid, addr)` tracking map |
| `libbpf_ring_output` | 加真实 BPF ringbuf record 输出 |

## pink 上运行

pink 已安装并确认 `libbpf-devel`、`clang`、`bpftool`、`pkg-config`、`libelf` 和 `/sys/kernel/btf/vmlinux`。`libbpf_ring_output` 使用真实 `BPF_MAP_TYPE_RINGBUF`，不再用 bpftrace `printf` 结果近似 ringbuf 路径。

```bash
cd /mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1
cmake -S . -B build -DLNHV1_ENABLE_LIBBPF=ON
cmake --build build -j

sudo LNHV1_DURATION=1 LNHV1_THREADS_LIST=1 \
  LNHV1_LIBBPF_MODE_LIST=libbpf_count_only,libbpf_sample_filter,libbpf_tracking \
  bash scripts/run_libbpf_uprobe_comparison.sh

sudo LNHV1_DURATION=5 LNHV1_THREADS_LIST=1,4 \
  LNHV1_LIBBPF_MODE_LIST=libbpf_count_only,libbpf_sample_filter,libbpf_tracking,libbpf_ring_output \
  LNHV1_CSV_PATH=results/libbpf_uprobe_comparison_server_2026-05-25.csv \
  bash scripts/run_libbpf_uprobe_comparison.sh
```

## 讲法

这轮不要把 eBPF 讲成已经替代 native_hook。更稳妥的说法是：bpftrace 已经给了一个外部观测基线，但它的输出路径和计数都有原型性质；libbpf 版本把关键变量收紧，尤其是 target pid 过滤和真实 ringbuf 输出。后面如果 libbpf ringbuf 仍然在四线程下有优势，才说明 eBPF 路线值得继续做更完整的 trace backend。

## 正式结果

正式 CSV：

- `linux_native_hook_v1/results/libbpf_uprobe_comparison_server_2026-05-25.csv`

参数：

- `threads=1,4`
- `size=32`
- `duration=5`
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`

| threads | mode | throughput_ops | overhead_pct |
|---:|---|---:|---:|
| 1 | baseline_no_hook | 16.938M | 0.00% |
| 1 | ld_preload_stage6_default | 0.275M | 98.37% |
| 1 | ld_preload_stage6_optimized | 0.710M | 95.81% |
| 1 | libbpf_count_only | 0.182M | 98.93% |
| 1 | libbpf_sample_filter | 0.178M | 98.95% |
| 1 | libbpf_tracking | 0.173M | 98.98% |
| 1 | libbpf_ring_output | 0.135M | 99.20% |
| 4 | baseline_no_hook | 14.035M | 0.00% |
| 4 | ld_preload_stage6_default | 0.059M | 99.58% |
| 4 | ld_preload_stage6_optimized | 0.308M | 97.80% |
| 4 | libbpf_count_only | 0.542M | 96.14% |
| 4 | libbpf_sample_filter | 0.524M | 96.27% |
| 4 | libbpf_tracking | 0.521M | 96.29% |
| 4 | libbpf_ring_output | 0.422M | 96.99% |

## 结果解读

单线程下，libbpf/uProbe 不是更快的路径。即使只做 `count_only`，吞吐也低于 optimized LD_PRELOAD Stage 6；加入 ringbuf 输出后进一步下降。这说明高频 malloc/free 场景里，单次 uProbe tax 本身很重。

四线程下，libbpf 表现明显更好。`libbpf_tracking` 是 optimized LD_PRELOAD Stage 6 的约 1.69x，真实 ringbuf 输出的 `libbpf_ring_output` 也有约 1.37x。这个结果支持之前的判断：eBPF 的价值不一定在单次 hook 更轻，而是在多线程下绕开了当前 producer 内部共享锁、writer/ring 和 consumer 语义的一部分成本。

`libbpf_ring_output` 两组都是 `ringbuf_drops=0`，`observed_events` 等于输出记录数，说明这轮真实 ringbuf drain 没有明显丢事件。当前仍只覆盖 `malloc/free`，没有 `calloc/realloc`、stack unwind 和完整 trace backend，所以结论仍是“eBPF 外部观测基线值得保留”，不是“已经替代 native_hook”。
