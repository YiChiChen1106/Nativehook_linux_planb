# native_hook libbpf/uProbe allocator coverage and repeat experiment

## 这轮回答什么

这轮把 libbpf/uProbe 对比从 `malloc/free` 扩到 `malloc/calloc/realloc/free`，同时做 3 次重复实验，看结论是否稳定。这里仍然把 eBPF 当成外部观测基线，不把它直接说成 native_hook 的替代方案。

## 实现变化

- `perf_test_data_linux` 增加 `--pattern malloc_only|mixed3`。
- `malloc_only` 保持原有默认 workload，兼容之前实验。
- `mixed3` 用确定性循环覆盖三类 allocator：
  - `malloc(size) + free`
  - `calloc(1, size) + free`
  - `malloc(size) + realloc(ptr, size * 2) + free`
- libbpf BPF 程序新增 `calloc`、`realloc` entry/return probes。
- loader 输出新增 `calloc_calls`、`realloc_calls` 和对应 sampled entry 计数。
- `run_libbpf_uprobe_comparison.sh` 增加：
  - `LNHV1_ALLOC_PATTERN_LIST`
  - `LNHV1_REPEAT`
  - CSV 字段 `alloc_pattern`、`repeat_index`、`calloc_events`、`realloc_events`

## 正式实验

正式 CSV：

- `linux_native_hook_v1/results/libbpf_uprobe_repeat_server_2026-05-26.csv`

参数：

- server: `pink`
- `threads=1,4`
- `size=32`
- `duration=5`
- `repeat=3`
- `alloc_pattern=malloc_only,mixed3`
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`

## 3 次重复均值

单位是 M ops/s，括号里是标准差。

### malloc_only

| threads | mode | mean throughput | overhead |
|---:|---|---:|---:|
| 1 | baseline_no_hook | 16.057 (0.167) | 0.00% |
| 1 | ld_preload_stage6_optimized | 0.674 (0.002) | 95.80% |
| 1 | libbpf_count_only | 0.172 (0.007) | 98.93% |
| 1 | libbpf_tracking | 0.157 (0.003) | 99.02% |
| 1 | libbpf_ring_output | 0.118 (0.005) | 99.27% |
| 4 | baseline_no_hook | 13.167 (0.401) | 0.00% |
| 4 | ld_preload_stage6_optimized | 0.305 (0.019) | 97.68% |
| 4 | libbpf_count_only | 0.577 (0.040) | 95.61% |
| 4 | libbpf_tracking | 0.537 (0.031) | 95.92% |
| 4 | libbpf_ring_output | 0.434 (0.025) | 96.70% |

### mixed3

| threads | mode | mean throughput | overhead |
|---:|---|---:|---:|
| 1 | baseline_no_hook | 11.979 (0.103) | 0.00% |
| 1 | ld_preload_stage6_optimized | 0.590 (0.009) | 95.07% |
| 1 | libbpf_count_only | 0.149 (0.002) | 98.75% |
| 1 | libbpf_tracking | 0.138 (0.003) | 98.85% |
| 1 | libbpf_ring_output | 0.111 (0.003) | 99.07% |
| 4 | baseline_no_hook | 9.246 (0.392) | 0.00% |
| 4 | ld_preload_stage6_optimized | 0.276 (0.024) | 97.01% |
| 4 | libbpf_count_only | 0.456 (0.008) | 95.06% |
| 4 | libbpf_tracking | 0.435 (0.005) | 95.29% |
| 4 | libbpf_ring_output | 0.361 (0.006) | 96.09% |

## 覆盖和稳定性检查

- `mixed3` 的计数符合预期：`calloc/realloc` 数量级约为 `malloc` 的一半，`free` 约为 `malloc + calloc`。
- `libbpf_ring_output` 所有正式行都是 `ringbuf_drops=0`。
- `libbpf_ring_output` 所有正式行里 `observed_events == output_records`。
- tracking 模式里 `unmatched_free` 最大为 39，主要来自运行时/线程库内部 allocator 行为，数量级很小。

## 结果口径

这轮重复实验没有改变 5 月 25 日的主结论：单线程下 libbpf/uProbe 明显慢于 optimized LD_PRELOAD；四线程下 libbpf 更好。

和 optimized LD_PRELOAD Stage 6 相比：

- `malloc_only, threads=1`：`libbpf_tracking` 约为 0.23x，`libbpf_ring_output` 约为 0.17x。
- `malloc_only, threads=4`：`libbpf_tracking` 约为 1.76x，`libbpf_ring_output` 约为 1.42x。
- `mixed3, threads=1`：`libbpf_tracking` 约为 0.23x，`libbpf_ring_output` 约为 0.19x。
- `mixed3, threads=4`：`libbpf_tracking` 约为 1.58x，`libbpf_ring_output` 约为 1.31x。

所以组会里可以这样讲：

> 我把 libbpf 版本从 malloc/free 扩到 malloc/calloc/realloc/free，并做了 3 次重复。结果说明 eBPF/uProbe 的单次 probe tax 仍然很重，单线程不是优势路径；但在四线程下，它绕开了当前 LD_PRELOAD producer 里的共享锁和 writer/ring 路径，因此整体吞吐仍然高于 optimized Stage 6。eBPF 现在更适合作为外部观测基线继续保留，而不是马上替代 native_hook。
