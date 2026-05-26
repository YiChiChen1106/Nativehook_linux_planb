# eBPF/uProbe comparison probe

This directory contains a minimal bpftrace-based comparison probe for the
Plan B producer hot-path experiments.

The goal is to compare an external uProbe observer with the current
LD_PRELOAD-based native_hook-style producer path. This is not a replacement for
the production native_hook path and does not include stack unwinding or a full
trace backend.

## Modes

- `ebpf_count_only`: attach entry uprobes to `malloc` and `free`, count calls.
- `ebpf_sample_filter`: add sample/filter decisions and `malloc` return probe.
- `ebpf_tracking`: add allocation/free pairing with a `(pid, address)` map key.
- `ebpf_ring_output`: emit compact per-record lines through bpftrace user-space
  output. This is a bpftrace output-path proxy, not a libbpf ringbuf
  implementation.

## Requirements

The current pink server has `clang`, `bpftool`, `bpftrace`, and BTF available,
but bpftrace requires root. Run the comparison script as root or with sudo.

If the experiment later needs a production-style BPF ring buffer reader, install
`libbpf-devel` and replace the bpftrace templates with a libbpf loader.

For non-root smoke testing of the baseline and LD_PRELOAD reference rows, set
`LNHV1_EBPF_MODE_LIST=none`.

## libbpf loader

The libbpf path is the next-step implementation for structured eBPF/uProbe
comparison. It keeps the bpftrace prototype intact and adds:

- `uprobe_probe.bpf.c`: BPF-side uProbe program.
- `uprobe_loader.cpp`: user-space loader that forks the workload, filters by
  target process id, attaches `malloc/free` uprobes, drains ringbuf events, and
  prints a single summary line.
- `uprobe_common.h`: shared ABI for modes, counters, config, and ringbuf events.

Build it explicitly on pink:

```bash
cmake -S . -B build -DLNHV1_ENABLE_LIBBPF=ON
cmake --build build -j
```

Run the dedicated comparison script as root or with equivalent BPF/uProbe
capabilities:

```bash
sudo LNHV1_DURATION=1 LNHV1_THREADS_LIST=1 \
  LNHV1_LIBBPF_MODE_LIST=libbpf_count_only,libbpf_sample_filter,libbpf_tracking \
  bash scripts/run_libbpf_uprobe_comparison.sh
```

The libbpf `libbpf_ring_output` mode uses a real BPF ring buffer. The older
bpftrace `ebpf_ring_output` mode remains only an output-path proxy.
