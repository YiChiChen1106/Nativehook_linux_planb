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
