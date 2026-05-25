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
