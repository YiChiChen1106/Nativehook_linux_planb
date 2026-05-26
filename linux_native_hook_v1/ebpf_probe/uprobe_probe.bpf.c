#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define LNHV1_BPF_BUILD 1
#include "ebpf_probe/uprobe_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

enum {
    kLnhv1AllocProbeMalloc = 1,
    kLnhv1AllocProbeCalloc = 2,
    kLnhv1AllocProbeRealloc = 3,
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct Lnhv1UprobeConfig);
} probe_config SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct Lnhv1UprobeStats);
} stats SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, __u32);
    __type(value, __u64);
} alloc_seq SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, __u32);
    __type(value, __u64);
} pending_alloc SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 262144);
    __type(key, struct Lnhv1AllocKey);
    __type(value, __u64);
} tracked_alloc SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} events SEC(".maps");

static __always_inline struct Lnhv1UprobeConfig *get_config(void)
{
    __u32 key = 0;
    return bpf_map_lookup_elem(&probe_config, &key);
}

static __always_inline struct Lnhv1UprobeStats *get_stats(void)
{
    __u32 key = 0;
    return bpf_map_lookup_elem(&stats, &key);
}

static __always_inline int target_matches(const struct Lnhv1UprobeConfig *cfg,
                                          __u64 pid_tgid)
{
    __u32 tgid = pid_tgid >> 32;
    return cfg->target_tgid == 0 || cfg->target_tgid == tgid;
}

static __always_inline int should_keep_alloc(const struct Lnhv1UprobeConfig *cfg,
                                             __u32 tid,
                                             __u64 size)
{
    __u64 one = 1;
    __u64 next = 1;
    __u64 *seq = bpf_map_lookup_elem(&alloc_seq, &tid);
    if (seq) {
        next = *seq + 1;
        bpf_map_update_elem(&alloc_seq, &tid, &next, BPF_ANY);
    } else {
        bpf_map_update_elem(&alloc_seq, &tid, &one, BPF_ANY);
    }

    if (cfg->sample_interval > 1 && next % cfg->sample_interval != 0) {
        return 0;
    }
    if (cfg->filter_size >= 0 && size < (__u64)cfg->filter_size) {
        return 0;
    }
    return 1;
}

static __always_inline void emit_record(__u32 type,
                                        __u32 tgid,
                                        __u32 tid,
                                        __u64 addr,
                                        __u64 size)
{
    struct Lnhv1UprobeStats *stat = get_stats();
    struct Lnhv1UprobeEvent *event;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        if (stat) {
            stat->ringbuf_drops += 1;
        }
        return;
    }

    event->timestamp_ns = bpf_ktime_get_ns();
    event->addr = addr;
    event->size = size;
    event->tgid = tgid;
    event->tid = tid;
    event->type = type;
    event->reserved = 0;
    bpf_get_current_comm(event->comm, sizeof(event->comm));
    bpf_ringbuf_submit(event, 0);

    if (stat) {
        stat->output_records += 1;
    }
}

static __always_inline void count_alloc_call(struct Lnhv1UprobeStats *stat,
                                             __u32 alloc_kind)
{
    if (!stat) {
        return;
    }

    if (alloc_kind == kLnhv1AllocProbeMalloc) {
        stat->malloc_calls += 1;
    } else if (alloc_kind == kLnhv1AllocProbeCalloc) {
        stat->calloc_calls += 1;
    } else if (alloc_kind == kLnhv1AllocProbeRealloc) {
        stat->realloc_calls += 1;
    }
}

static __always_inline void count_sampled_alloc_entry(struct Lnhv1UprobeStats *stat,
                                                      __u32 alloc_kind)
{
    if (!stat) {
        return;
    }

    if (alloc_kind == kLnhv1AllocProbeMalloc) {
        stat->sampled_malloc_entries += 1;
    } else if (alloc_kind == kLnhv1AllocProbeCalloc) {
        stat->sampled_calloc_entries += 1;
    } else if (alloc_kind == kLnhv1AllocProbeRealloc) {
        stat->sampled_realloc_entries += 1;
    }
}

static __always_inline int handle_alloc_entry_common(struct pt_regs *ctx,
                                                     __u64 size,
                                                     __u32 alloc_kind)
{
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tid = (__u32)pid_tgid;
    struct Lnhv1UprobeConfig *cfg = get_config();
    struct Lnhv1UprobeStats *stat = get_stats();

    (void)ctx;

    if (!cfg || !target_matches(cfg, pid_tgid)) {
        return 0;
    }

    count_alloc_call(stat, alloc_kind);

    if (cfg->mode < kLnhv1ModeSampleFilter) {
        return 0;
    }

    if (should_keep_alloc(cfg, tid, size)) {
        bpf_map_update_elem(&pending_alloc, &tid, &size, BPF_ANY);
        count_sampled_alloc_entry(stat, alloc_kind);
    } else {
        bpf_map_delete_elem(&pending_alloc, &tid);
    }
    return 0;
}

static __always_inline int handle_alloc_return_common(struct pt_regs *ctx)
{
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tgid = pid_tgid >> 32;
    __u32 tid = (__u32)pid_tgid;
    __u64 addr = PT_REGS_RC(ctx);
    struct Lnhv1UprobeConfig *cfg = get_config();
    struct Lnhv1UprobeStats *stat = get_stats();
    struct Lnhv1AllocKey key = {};
    __u64 alloc_size;
    __u64 *size;

    if (!cfg || !target_matches(cfg, pid_tgid) ||
        cfg->mode < kLnhv1ModeSampleFilter) {
        return 0;
    }

    size = bpf_map_lookup_elem(&pending_alloc, &tid);
    if (!size) {
        return 0;
    }

    if (stat) {
        stat->sampled_alloc_returns += 1;
    }

    if (addr != 0 && cfg->mode >= kLnhv1ModeTracking) {
        alloc_size = *size;
        key.tgid = tgid;
        key.addr = addr;
        bpf_map_update_elem(&tracked_alloc, &key, &alloc_size, BPF_ANY);
        if (stat) {
            stat->alloc_records += 1;
        }

        if (cfg->mode >= kLnhv1ModeRingOutput) {
            emit_record(kLnhv1EventAlloc, tgid, tid, addr, alloc_size);
        }
    }

    bpf_map_delete_elem(&pending_alloc, &tid);
    return 0;
}

SEC("uprobe/malloc")
int handle_malloc_entry(struct pt_regs *ctx)
{
    return handle_alloc_entry_common(ctx,
                                     PT_REGS_PARM1(ctx),
                                     kLnhv1AllocProbeMalloc);
}

SEC("uretprobe/malloc")
int handle_malloc_return(struct pt_regs *ctx)
{
    return handle_alloc_return_common(ctx);
}

SEC("uprobe/calloc")
int handle_calloc_entry(struct pt_regs *ctx)
{
    __u64 nmemb = PT_REGS_PARM1(ctx);
    __u64 item_size = PT_REGS_PARM2(ctx);
    return handle_alloc_entry_common(ctx,
                                     nmemb * item_size,
                                     kLnhv1AllocProbeCalloc);
}

SEC("uretprobe/calloc")
int handle_calloc_return(struct pt_regs *ctx)
{
    return handle_alloc_return_common(ctx);
}

SEC("uprobe/realloc")
int handle_realloc_entry(struct pt_regs *ctx)
{
    return handle_alloc_entry_common(ctx,
                                     PT_REGS_PARM2(ctx),
                                     kLnhv1AllocProbeRealloc);
}

SEC("uretprobe/realloc")
int handle_realloc_return(struct pt_regs *ctx)
{
    return handle_alloc_return_common(ctx);
}

SEC("uprobe/free")
int handle_free_entry(struct pt_regs *ctx)
{
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tgid = pid_tgid >> 32;
    __u32 tid = (__u32)pid_tgid;
    __u64 addr = PT_REGS_PARM1(ctx);
    struct Lnhv1UprobeConfig *cfg = get_config();
    struct Lnhv1UprobeStats *stat = get_stats();
    struct Lnhv1AllocKey key = {};
    __u64 *size;

    if (!cfg || !target_matches(cfg, pid_tgid)) {
        return 0;
    }

    if (stat) {
        stat->free_calls += 1;
    }

    if (cfg->mode < kLnhv1ModeTracking || addr == 0) {
        return 0;
    }

    key.tgid = tgid;
    key.addr = addr;
    size = bpf_map_lookup_elem(&tracked_alloc, &key);
    if (!size) {
        if (stat) {
            stat->unmatched_frees += 1;
        }
        return 0;
    }

    if (stat) {
        stat->matched_frees += 1;
    }

    if (cfg->mode >= kLnhv1ModeRingOutput) {
        emit_record(kLnhv1EventFree, tgid, tid, addr, *size);
    }

    bpf_map_delete_elem(&tracked_alloc, &key);
    return 0;
}
