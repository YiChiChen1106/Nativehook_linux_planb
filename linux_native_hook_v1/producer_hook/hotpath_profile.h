#pragma once

#include <cstdint>
#include <pthread.h>

namespace linux_native_hook_v1 {

enum class HotpathProfileSegment : uint8_t {
    kHookEntry = 0,
    kGuard,
    kRecordAllocTotal,
    kRecordFreeTotal,
    kWriterMutexWait,
    kWriterMutexHold,
    kTrackingShardMutexWait,
    kTrackingShardMutexHold,
    kSampleFilter,
    kTrackingInsert,
    kTrackingLookup,
    kTrackingErase,
    kRecordFill,
    kMetadataClock,
    kMetadataPid,
    kMetadataTid,
    kThreadName,
    kRingIndexCheck,
    kShmRecordCopy,
    kAtomicIndexUpdate,
    kNotify,
    kWaitDrain,
    kConnection,
    kCount,
};

bool HotpathProfileEnabled();
uint64_t HotpathProfileStart();
void HotpathProfileAdd(HotpathProfileSegment segment, uint64_t start_cycles);
void HotpathProfileDumpIfEnabled();

class HotpathProfileScope {
public:
    explicit HotpathProfileScope(HotpathProfileSegment segment)
        : segment_(segment)
        , start_cycles_(HotpathProfileStart())
    {
    }

    ~HotpathProfileScope()
    {
        HotpathProfileAdd(segment_, start_cycles_);
    }

    HotpathProfileScope(const HotpathProfileScope&) = delete;
    HotpathProfileScope& operator=(const HotpathProfileScope&) = delete;

private:
    HotpathProfileSegment segment_;
    uint64_t start_cycles_;
};

class HotpathProfileMutexGuard {
public:
    HotpathProfileMutexGuard(
        pthread_mutex_t* mutex,
        HotpathProfileSegment wait_segment,
        HotpathProfileSegment hold_segment);
    ~HotpathProfileMutexGuard();

    HotpathProfileMutexGuard(const HotpathProfileMutexGuard&) = delete;
    HotpathProfileMutexGuard& operator=(const HotpathProfileMutexGuard&) = delete;

    void Unlock();

private:
    pthread_mutex_t* mutex_ = nullptr;
    HotpathProfileSegment hold_segment_;
    uint64_t hold_start_cycles_ = 0;
    bool locked_ = false;
};

}  // namespace linux_native_hook_v1
