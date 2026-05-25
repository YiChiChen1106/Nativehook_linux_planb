# native_hook eBPF/uProbe 对比实验说明

## 这轮实验想回答什么

这条线把 eBPF 定位成外部观测方案对比，而不是直接替代当前 Plan B producer hot path。核心问题是：

> 不用 `LD_PRELOAD` 在进程内写 record，而是用 eBPF/uProbe 从进程外观测 `malloc/free`，它的最小探针成本、tracking 成本和输出成本分别有多大？

## 实验实现

- 新增 `linux_native_hook_v1/ebpf_probe/`，使用 bpftrace template。
- 新增 `scripts/run_ebpf_uprobe_comparison.sh`，同一份 CSV 里输出：
  - `baseline_no_hook`
  - `ld_preload_stage6_default`
  - `ld_preload_stage6_optimized`
  - `ebpf_count_only`
  - `ebpf_sample_filter`
  - `ebpf_tracking`
  - `ebpf_ring_output`
- eBPF probe 先只覆盖 `malloc/free`：
  - `malloc` entry 读取 requested size。
  - `malloc` return 读取返回地址。
  - `free` entry 读取地址。
  - tracking key 使用 `(pid, addr)`。
  - metadata 使用 bpftrace/eBPF 可见的 `pid/tid/nsecs/comm`。

## pink 环境与权限

已确认 pink 上有：

- `clang`
- `bpftool`
- `bpftrace`
- BTF: `/sys/kernel/btf`

但当前 bpftrace 要求 root，普通用户直接运行会失败。因此正式实验需要：

- 用 sudo/root 跑 `scripts/run_ebpf_uprobe_comparison.sh`，或
- 让管理员开放 eBPF/uProbe 运行权限，或
- 安装 `libbpf-devel` 后改成 libbpf loader。

给管理员的最小需求可以这样提：

```text
我需要在 pink 上运行 malloc/free uProbe 的 eBPF 对比实验。
目前机器已有 bpftrace/bpftool/clang/BTF，但 bpftrace 需要 root。
请临时允许我用 sudo 运行 bpftrace，或提供 root 协助执行脚本。
如果后续要做 libbpf 版本，再安装 libbpf-devel。
```

## 建议正式命令

```bash
cd /mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1
cmake -S . -B build && cmake --build build -j

sudo LNHV1_DURATION=1 LNHV1_THREADS_LIST=1 \
  LNHV1_EBPF_MODE_LIST=ebpf_count_only,ebpf_sample_filter,ebpf_tracking \
  bash scripts/run_ebpf_uprobe_comparison.sh

sudo LNHV1_DURATION=5 LNHV1_THREADS_LIST=1,4 \
  bash scripts/run_ebpf_uprobe_comparison.sh
```

## 解释口径

这组实验不要预设 eBPF 一定更快。高频 malloc/free 场景里，每次 uProbe 都会进入内核侧观测路径，最小探针成本可能不低。eBPF 更可能的价值是部署和隔离：不需要 `LD_PRELOAD`，观测逻辑在目标进程外面。

组会里可以这样讲：

> 我会把 eBPF 当作外部观测基线来做对比，不预设它一定替代 native_hook。实验先测最小 uProbe tax，再逐步加 sample/filter、tracking 和 bpftrace 输出路径，最后和当前 Plan B optimized producer 路径放在同一张表里比较。

## 正式结果

正式 CSV：

- `linux_native_hook_v1/results/ebpf_uprobe_comparison_server_2026-05-25.csv`

实验参数：

- 服务器：`pink`
- workload：`perf_test_data_linux`
- `threads=1,4`
- `size=32`
- `duration=5`
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`

### 吞吐结果

| threads | mode | throughput_ops | overhead_pct |
|---:|---|---:|---:|
| 1 | baseline_no_hook | 16.704M | 0.00% |
| 1 | ld_preload_stage6_default | 0.263M | 98.42% |
| 1 | ld_preload_stage6_optimized | 0.646M | 96.14% |
| 1 | ebpf_count_only | 0.219M | 98.69% |
| 1 | ebpf_sample_filter | 0.174M | 98.96% |
| 1 | ebpf_tracking | 0.166M | 99.00% |
| 4 | baseline_no_hook | 13.822M | 0.00% |
| 4 | ld_preload_stage6_default | 0.050M | 99.64% |
| 4 | ld_preload_stage6_optimized | 0.309M | 97.76% |
| 4 | ebpf_count_only | 0.545M | 96.06% |
| 4 | ebpf_sample_filter | 0.420M | 96.96% |
| 4 | ebpf_tracking | 0.398M | 97.12% |

### 结果解读

1. 单线程下，eBPF/uProbe 不是更快路径。
   `ebpf_count_only` 只有 0.219M，比 `ld_preload_stage6_default` 的 0.263M 还低，也明显低于 `ld_preload_stage6_optimized` 的 0.646M。说明高频 malloc/free 场景里，最小 uProbe tax 本身就很重。

2. 四线程下，eBPF/uProbe 有并发侧优势。
   `ebpf_count_only` 是 optimized LD_PRELOAD Stage 6 的 1.76x，`ebpf_tracking` 也是 1.29x。这说明 eBPF 外部观测路径避开了当前 producer 内部的一部分共享锁、writer/ring 和 consumer 语义成本。

3. 这不是等价替代结论。
   当前 eBPF 原型只覆盖 `malloc/free`，没有 `calloc/realloc`、没有 native_hook 的 consumer drain 语义、没有完整 trace backend，也没有 stack unwind。`ebpf_count_only/sample_filter/tracking` 主要是定位 uProbe 和 map 操作成本，不等价于完整 native_hook 输出路径。

4. 暂时不建议把 eBPF 说成“整体更优”。
   更稳妥的结论是：eBPF 值得作为外部观测基线保留。它在单线程 record-dense 场景很重，但四线程下避开进程内共享路径后有优势。下一步如果继续做 eBPF，应补 `ring_output/libbpf ringbuf` 版本，而不是用 bpftrace `printf` 当最终输出路径。

补充说明：当前 bpftrace 版本的 `malloc/free` event counter 只作为辅助诊断。多线程下它会观测到目标进程运行时和 libc 内部的额外 allocator 流量，不能把计数严格解释成 workload 的一轮 malloc/free 配对；这组结果主要看 throughput。

## 组会口径

这组实验可以这样讲：

> 我把 eBPF 作为外部观测基线测了一轮，不预设它一定更快。结果比较分裂：单线程下，最小 count-only uProbe 就比当前 optimized LD_PRELOAD 慢很多；但四线程下，eBPF tracking 反而比 optimized Stage 6 高大约 1.3 倍。我的理解是，eBPF 的优势不在单次 malloc/free 的热路径成本，而是在它绕开了当前 producer 里的共享锁和 writer/ring 路径。不过现在这个原型语义还比 native_hook 弱，所以只能说 eBPF 值得作为对比路线继续保留，不能说它已经替代 native_hook。
