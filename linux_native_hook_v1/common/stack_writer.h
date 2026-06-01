#pragma once

#include <cstddef>
#include <cstdint>

#include "common/shm_layout.h"

namespace linux_native_hook_v1 {

class StackWriter {
public:
    StackWriter() = default;

    void SetSharedMemory(ShmHeader* header, HookRecord* records, uint32_t flush_threshold)
    {
        header_ = header;
        records_ = records;
        flush_threshold_ = flush_threshold;
    }

    void SetEventFd(int event_fd)
    {
        event_fd_ = event_fd;
    }

    bool Write(const HookRecord* records, uint32_t record_count);

    bool WriteSingle(const HookRecord& record, bool self_drain)
    {
        return Write(&record, 1, self_drain);
    }

    bool Write(const HookRecord* records, uint32_t record_count, bool self_drain);

    bool PrepareFlush();

    bool Flush();

    bool FlushEventFd();

    uint32_t pending_count() const { return pending_count_; }

private:
    ShmHeader* header_ = nullptr;
    HookRecord* records_ = nullptr;
    int event_fd_ = -1;
    uint32_t flush_threshold_ = 0;
    uint32_t pending_count_ = 0;
};

}  // namespace linux_native_hook_v1
