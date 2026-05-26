#ifndef LNHV1_EBPF_PROBE_UPROBE_COMMON_H_
#define LNHV1_EBPF_PROBE_UPROBE_COMMON_H_

#ifdef LNHV1_BPF_BUILD
typedef __u32 lnhv1_u32;
typedef __u64 lnhv1_u64;
typedef __s64 lnhv1_s64;
#elif defined(__cplusplus)
#include <cstdint>
using lnhv1_u32 = std::uint32_t;
using lnhv1_u64 = std::uint64_t;
using lnhv1_s64 = std::int64_t;
#else
#include <stdint.h>
typedef uint32_t lnhv1_u32;
typedef uint64_t lnhv1_u64;
typedef int64_t lnhv1_s64;
#endif

enum {
    kLnhv1ModeCountOnly = 1,
    kLnhv1ModeSampleFilter = 2,
    kLnhv1ModeTracking = 3,
    kLnhv1ModeRingOutput = 4,
};

enum {
    kLnhv1EventAlloc = 1,
    kLnhv1EventFree = 2,
};

enum {
    kLnhv1CommSize = 16,
};

struct Lnhv1UprobeConfig {
    lnhv1_u32 mode;
    lnhv1_u32 sample_interval;
    lnhv1_s64 filter_size;
    lnhv1_u32 target_tgid;
    lnhv1_u32 reserved;
};

struct Lnhv1AllocKey {
    lnhv1_u32 tgid;
    lnhv1_u32 reserved;
    lnhv1_u64 addr;
};

struct Lnhv1UprobeStats {
    lnhv1_u64 malloc_calls;
    lnhv1_u64 calloc_calls;
    lnhv1_u64 realloc_calls;
    lnhv1_u64 free_calls;
    lnhv1_u64 sampled_malloc_entries;
    lnhv1_u64 sampled_calloc_entries;
    lnhv1_u64 sampled_realloc_entries;
    lnhv1_u64 sampled_alloc_returns;
    lnhv1_u64 alloc_records;
    lnhv1_u64 matched_frees;
    lnhv1_u64 unmatched_frees;
    lnhv1_u64 output_records;
    lnhv1_u64 ringbuf_drops;
};

struct Lnhv1UprobeEvent {
    lnhv1_u64 timestamp_ns;
    lnhv1_u64 addr;
    lnhv1_u64 size;
    lnhv1_u32 tgid;
    lnhv1_u32 tid;
    lnhv1_u32 type;
    lnhv1_u32 reserved;
    char comm[kLnhv1CommSize];
};

#endif
