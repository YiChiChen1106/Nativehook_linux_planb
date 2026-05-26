#include "ebpf_probe/uprobe_common.h"

#include <cstddef>
#include <cstdint>

int main()
{
    static_assert(kLnhv1ModeCountOnly == 1, "count-only mode must be stable");
    static_assert(kLnhv1ModeSampleFilter == 2, "sample/filter mode must be stable");
    static_assert(kLnhv1ModeTracking == 3, "tracking mode must be stable");
    static_assert(kLnhv1ModeRingOutput == 4, "ring-output mode must be stable");

    static_assert(kLnhv1EventAlloc == 1, "alloc event type must be stable");
    static_assert(kLnhv1EventFree == 2, "free event type must be stable");
    static_assert(kLnhv1CommSize == 16, "BPF task comm size should match TASK_COMM_LEN");

    static_assert(offsetof(Lnhv1UprobeEvent, comm) % alignof(std::uint64_t) == 0,
                  "comm field should remain naturally aligned after integer metadata");
    static_assert(sizeof(Lnhv1UprobeEvent) <= 64,
                  "ringbuf event should remain compact for hot-path experiments");
    return 0;
}
