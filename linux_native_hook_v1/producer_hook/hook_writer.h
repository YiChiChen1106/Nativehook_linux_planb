#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <pthread.h>
#include <unordered_map>
#include <unordered_set>

#include "common/shm_layout.h"
#include "common/stack_writer.h"
#include "common/unix_defs.h"
#include "producer_hook/hook_socket_client.h"

namespace linux_native_hook_v1 {

class HookWriter {
public:
    static HookWriter& Instance();

    bool RecordAlloc(void* ptr, size_t size);
    bool RecordFree(void* ptr);
    void Flush();

private:
    static constexpr size_t kTrackingShardCount = 64;

    struct TrackingShard {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        std::unordered_set<uint64_t> allocations;
    };

    struct OwnershipShard {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        std::unordered_map<uint64_t, uint64_t> ownership_by_addr;
    };

    HookWriter();

    HookWriter(const HookWriter&) = delete;
    HookWriter& operator=(const HookWriter&) = delete;

    bool EnsureConnectedLocked();
    bool EnsureConnectedWithWriterLock();
    bool EnsureConnectedForImpactSubAblation();
    bool HasConnectionWithWriterLock();
    bool ShouldRecordAllocLocked(size_t size);
    bool HasTrackedAllocLocked(uint64_t addr) const;
    bool ConsumeTrackedAllocLocked(uint64_t addr);
    TrackingShard& TrackingShardFor(uint64_t addr);
    bool HasTrackedAllocSharded(uint64_t addr);
    bool ConsumeTrackedAllocSharded(uint64_t addr);
    void InsertTrackedAllocSharded(uint64_t addr);
    OwnershipShard& OwnershipShardFor(uint64_t addr);
    bool RecordTrackingAblationAllocLocked(uint64_t addr, size_t size, int sub_ablation_stage);
    bool RecordTrackingAblationFreeLocked(uint64_t addr, int sub_ablation_stage);
    bool RecordTrackingAblationAllocSharded(uint64_t addr, size_t size, int sub_ablation_stage);
    bool RecordTrackingAblationFreeSharded(uint64_t addr, int sub_ablation_stage);
    bool RecordWriteSubAblationAllocLocked(void* ptr, size_t size, int sub_ablation_stage);
    bool RecordWriteSubAblationFreeLocked(void* ptr, int sub_ablation_stage);
    bool RecordWriteSubAblationAllocSharded(void* ptr, size_t size, int sub_ablation_stage);
    bool RecordWriteSubAblationFreeSharded(void* ptr, int sub_ablation_stage);
    bool RecordAllocSharded(void* ptr, size_t size, int ablation_stage);
    bool RecordFreeSharded(void* ptr, int ablation_stage);
    bool HasTrackedAllocThreadLocal(bool use_fallback, uint64_t addr);
    bool ConsumeTrackedAllocThreadLocal(bool use_fallback, uint64_t addr);
    void InsertTrackedAllocThreadLocal(bool use_fallback, uint64_t addr);
    void RecordOwnershipThreadLocal(uint64_t addr, uint64_t owner_id);
    bool HasTrackedAllocOwnershipThreadLocal(uint64_t addr, uint64_t owner_id);
    bool ConsumeTrackedAllocOwnershipThreadLocal(uint64_t addr, uint64_t owner_id);
    bool RecordTrackingAblationAllocThreadLocal(
        bool use_fallback, uint64_t addr, size_t size, int sub_ablation_stage);
    bool RecordTrackingAblationFreeThreadLocal(bool use_fallback, uint64_t addr, int sub_ablation_stage);
    bool RecordWriteSubAblationAllocThreadLocal(
        bool use_fallback, void* ptr, size_t size, int sub_ablation_stage);
    bool RecordWriteSubAblationFreeThreadLocal(bool use_fallback, void* ptr, int sub_ablation_stage);
    bool RecordStage6WriterRingImpactAllocThreadLocal(
        bool use_fallback, void* ptr, size_t size, int sub_ablation_stage);
    bool RecordStage6WriterRingImpactFreeThreadLocal(bool use_fallback, void* ptr, int sub_ablation_stage);
    bool RecordStackWriterSubAblationAllocThreadLocal(
        bool use_fallback, void* ptr, size_t size, int sub_ablation_stage);
    bool RecordStackWriterSubAblationFreeThreadLocal(
        bool use_fallback, void* ptr, int sub_ablation_stage);
    bool RecordAllocThreadLocal(bool use_fallback, void* ptr, size_t size, int ablation_stage);
    bool RecordFreeThreadLocal(bool use_fallback, void* ptr, int ablation_stage);
    bool WriteRecordSubAblationLocked(const HookRecord& record, int sub_ablation_stage);
    bool WriteStage6WriterRingImpactLocked(const HookRecord& record, int sub_ablation_stage);
    void FillRecordForSubAblationLocked(
        HookRecord* record, HookEventType type, uint64_t addr, uint64_t size, int sub_ablation_stage);
    void FillStage6OptimizedRecord(HookRecord* record, HookEventType type, uint64_t addr, uint64_t size);
    void MaybeWriteThreadNameSubAblationLocked(int sub_ablation_stage);
    void MaybeWriteThreadNameLocked(int ablation_stage);
    void WaitUntilDrainedLocked() const;
    bool BufferStage6Record(const HookRecord& record, uint32_t batch_size);
    bool FlushStage6Batch(bool allow_notify);

public:
    void FlushStackWriterBatch();

private:
    bool WriteRecordsLocked(
        const HookRecord* records,
        uint32_t record_count,
        bool allow_notify,
        bool self_drain,
        bool* notify_after_unlock = nullptr);
    bool WriteRecordLocked(
        const HookRecord& record,
        bool allow_notify,
        bool self_drain,
        bool* notify_after_unlock = nullptr);
    void NotifyLocked();
    bool NotifyEventFd();

    StackWriter stack_writer_;
    HookSocketClient hook_socket_client_;
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
    std::atomic<bool> connected_fast_path_ {false};
    std::unordered_set<uint64_t> tracked_allocations_;
    std::array<TrackingShard, kTrackingShardCount> tracking_shards_;
    std::array<OwnershipShard, kTrackingShardCount> ownership_shards_;
};

}  // namespace linux_native_hook_v1
