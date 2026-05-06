#pragma once

#include <cstddef>
#include <cstdint>
#include <pthread.h>
#include <unordered_set>

#include "common/shm_layout.h"
#include "common/unix_defs.h"

namespace linux_native_hook_v1 {

class HookWriter {
public:
    static HookWriter& Instance();

    bool RecordAlloc(void* ptr, size_t size);
    bool RecordFree(void* ptr);
    void Flush();

private:
    HookWriter();

    HookWriter(const HookWriter&) = delete;
    HookWriter& operator=(const HookWriter&) = delete;

    bool EnsureConnectedLocked();
    bool ShouldRecordAllocLocked(size_t size);
    bool ConsumeTrackedAllocLocked(uint64_t addr);
    void MaybeWriteThreadNameLocked(int ablation_stage);
    void WaitUntilDrainedLocked() const;
    bool WriteRecordLocked(const HookRecord& record, bool allow_notify, bool self_drain);
    void NotifyLocked();

    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    int control_fd_ = -1;
    int shm_fd_ = -1;
    int event_fd_ = -1;
    void* mapping_ = nullptr;
    size_t mapped_size_ = 0;
    ShmHeader* header_ = nullptr;
    HookRecord* records_ = nullptr;
    uint32_t pending_count_ = 0;
    uint32_t flush_threshold_ = 0;
    uint32_t sample_interval_ = kDefaultSampleInterval;
    int32_t clock_id_ = CLOCK_REALTIME;
    int32_t filter_size_ = kDefaultFilterSize;
    bool is_blocked_ = false;
    const char* socket_path_ = nullptr;
    std::unordered_set<uint64_t> tracked_allocations_;
};

}  // namespace linux_native_hook_v1
