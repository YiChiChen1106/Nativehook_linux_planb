#include "producer_hook/hook_writer.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/mman.h>
#include <sys/prctl.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "common/socket_fd.h"
#include "common/unix_defs.h"
#include "producer_hook/ablation_config.h"
#include "producer_hook/hook_guard.h"
#include "producer_hook/hotpath_profile.h"

namespace linux_native_hook_v1 {

namespace {

timespec NowTs(clockid_t clock_id)
{
    const uint64_t profile_start = HotpathProfileStart();
    timespec ts {};
    clock_gettime(clock_id, &ts);
    HotpathProfileAdd(HotpathProfileSegment::kMetadataClock, profile_start);
    return ts;
}

uint32_t CurrentPid()
{
    const uint64_t profile_start = HotpathProfileStart();
    const uint32_t pid = static_cast<uint32_t>(getpid());
    HotpathProfileAdd(HotpathProfileSegment::kMetadataPid, profile_start);
    return pid;
}

uint32_t CurrentTid()
{
    const uint64_t profile_start = HotpathProfileStart();
    const uint32_t tid = static_cast<uint32_t>(syscall(SYS_gettid));
    HotpathProfileAdd(HotpathProfileSegment::kMetadataTid, profile_start);
    return tid;
}

const char* ResolveSocketPath()
{
    const char* path = std::getenv("LNHV1_SOCKET_PATH");
    return (path != nullptr && path[0] != '\0') ? path : kDefaultSocketPath;
}

struct ThreadTrackingContext {
    std::unordered_set<uint64_t> allocations;
};

thread_local uint32_t g_thread_name_counter = 0;
thread_local uint32_t g_sample_counter = 0;
thread_local uint32_t g_cached_tid = 0;
thread_local ThreadTrackingContext* g_thread_tracking = nullptr;
std::atomic<uint32_t> g_cached_pid {0};
pthread_once_t g_pid_tid_cache_atfork_once = PTHREAD_ONCE_INIT;
constexpr uint32_t kThreadNameRefreshInterval = 1000;

bool PassesFilter(int32_t filter_size, size_t size)
{
    return filter_size < 0 || size >= static_cast<size_t>(filter_size);
}

bool ShouldSample(uint32_t sample_interval)
{
    if (sample_interval <= 1) {
        return true;
    }
    const bool keep = (g_sample_counter % sample_interval) == 0;
    g_sample_counter = (g_sample_counter == UINT32_MAX) ? 0 : (g_sample_counter + 1);
    return keep;
}

size_t TrackingShardIndex(uint64_t addr)
{
    return static_cast<size_t>(((addr >> 4) ^ (addr >> 12) ^ (addr >> 20)) & 63U);
}

ThreadTrackingContext& ThreadTracking()
{
    if (g_thread_tracking == nullptr) {
        g_thread_tracking = new ThreadTrackingContext();
    }
    return *g_thread_tracking;
}

uint64_t ThreadTrackingOwnerId(const ThreadTrackingContext& context)
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&context));
}

bool IsRecordWriteSubAblationStage(int sub_ablation_stage)
{
    return (sub_ablation_stage >= kRecordWriteSubStageRecordFillMinimal &&
               sub_ablation_stage <= kRecordWriteSubStageAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kRecordWriteSubStageMetadataPidOnly &&
            sub_ablation_stage <= kRecordWriteSubStageMetadataCachedPidThreadLocalTid) ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedThreadNameNoRing &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kWriterRingSubStageWriterLockOnly &&
            sub_ablation_stage <= kWriterRingSubStageAtomicIndexSelfDrain);
}

bool IncludesClockMetadata(int sub_ablation_stage)
{
    return (sub_ablation_stage >= kRecordWriteSubStageMetadataClock &&
               sub_ablation_stage <= kRecordWriteSubStageAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kRecordWriteSubStageMetadataPidOnly &&
            sub_ablation_stage <= kRecordWriteSubStageMetadataCachedPidThreadLocalTid) ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedThreadNameNoRing &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kWriterRingSubStageRecordFillCached &&
            sub_ablation_stage <= kWriterRingSubStageAtomicIndexSelfDrain);
}

bool IncludesSyscallPidTidMetadata(int sub_ablation_stage)
{
    return sub_ablation_stage >= kRecordWriteSubStageMetadataPidTid &&
        sub_ablation_stage <= kRecordWriteSubStageAtomicIndexSelfDrain;
}

bool IncludesThreadNameMetadata(int sub_ablation_stage)
{
    return (sub_ablation_stage >= kRecordWriteSubStageMetadataThreadName &&
               sub_ablation_stage <= kRecordWriteSubStageAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedThreadNameNoRing &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kWriterRingSubStageThreadNameNoRing &&
            sub_ablation_stage <= kWriterRingSubStageAtomicIndexSelfDrain);
}

bool IncludesRingWritePath(int sub_ablation_stage)
{
    return (sub_ablation_stage >= kRecordWriteSubStageRingIndexCheck &&
               sub_ablation_stage <= kRecordWriteSubStageAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedRingIndexCheck &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kWriterRingSubStageRingIndexCheck &&
            sub_ablation_stage <= kWriterRingSubStageAtomicIndexSelfDrain);
}

bool IsRingIndexCheckOnlyStage(int sub_ablation_stage)
{
    return sub_ablation_stage == kRecordWriteSubStageRingIndexCheck ||
        sub_ablation_stage == kRecordWriteSubStageCachedRingIndexCheck ||
        sub_ablation_stage == kWriterRingSubStageRingIndexCheck;
}

bool IsShmRecordCopyOnlyStage(int sub_ablation_stage)
{
    return sub_ablation_stage == kRecordWriteSubStageShmRecordCopy ||
        sub_ablation_stage == kRecordWriteSubStageCachedShmRecordCopy ||
        sub_ablation_stage == kWriterRingSubStageShmRecordCopy;
}

bool UsesCachedPidTidMetadata(int sub_ablation_stage)
{
    return sub_ablation_stage == kRecordWriteSubStageMetadataCachedPidThreadLocalTid ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedThreadNameNoRing &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kWriterRingSubStageRecordFillCached &&
            sub_ablation_stage <= kWriterRingSubStageAtomicIndexSelfDrain);
}

void ResetPidTidCacheInChild()
{
    g_cached_pid.store(0, std::memory_order_release);
    g_cached_tid = 0;
}

void RegisterPidTidCacheAtFork()
{
    pthread_atfork(nullptr, nullptr, ResetPidTidCacheInChild);
}

uint32_t CachedPid()
{
    pthread_once(&g_pid_tid_cache_atfork_once, RegisterPidTidCacheAtFork);

    uint32_t pid = g_cached_pid.load(std::memory_order_acquire);
    if (pid != 0) {
        return pid;
    }

    pid = CurrentPid();
    uint32_t expected = 0;
    g_cached_pid.compare_exchange_strong(expected, pid, std::memory_order_release, std::memory_order_relaxed);
    return g_cached_pid.load(std::memory_order_acquire);
}

uint32_t ThreadLocalTid()
{
    pthread_once(&g_pid_tid_cache_atfork_once, RegisterPidTidCacheAtFork);

    if (g_cached_tid == 0) {
        g_cached_tid = CurrentTid();
    }
    return g_cached_tid;
}

uint32_t MetadataPid(bool use_cache)
{
    return use_cache ? CachedPid() : CurrentPid();
}

uint32_t MetadataTid(bool use_cache)
{
    return use_cache ? ThreadLocalTid() : CurrentTid();
}

}  // namespace

static void FillThreadNameRecord(HookRecord* record, int32_t clock_id, bool use_pid_tid_cache);

HookWriter::HookWriter()
    : flush_threshold_(kDefaultFlushThreshold)
{
}

HookWriter& HookWriter::Instance()
{
    // Intentionally leaked. Preload libraries are fragile during process teardown,
    // and keeping this singleton alive avoids destructor-order crashes.
    static HookWriter* writer = new HookWriter();
    return *writer;
}

bool HookWriter::EnsureConnectedLocked()
{
    HotpathProfileScope profile_scope(HotpathProfileSegment::kConnection);

    if (header_ != nullptr) {
        return true;
    }

    if (socket_path_ == nullptr) {
        socket_path_ = ResolveSocketPath();
    }

    control_fd_ = ConnectUnixSocket(socket_path_);
    if (control_fd_ < 0) {
        return false;
    }

    const int32_t pid = static_cast<int32_t>(CurrentPid());
    if (!SendBytes(control_fd_, &pid, sizeof(pid))) {
        return false;
    }

    ControlConfig config {};
    if (!RecvBytes(control_fd_, &config, sizeof(config))) {
        return false;
    }
    if (!RecvFd(control_fd_, &shm_fd_)) {
        return false;
    }
    if (!RecvFd(control_fd_, &event_fd_)) {
        return false;
    }

    mapped_size_ = ShmBytesForCapacity(config.ring_capacity);
    mapping_ = mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        return false;
    }

    header_ = GetShmHeader(mapping_);
    records_ = GetShmRecords(mapping_);
    flush_threshold_ = config.flush_threshold == 0 ? kDefaultFlushThreshold : config.flush_threshold;
    sample_interval_ = config.sample_interval == 0 ? kDefaultSampleInterval : config.sample_interval;
    clock_id_ = config.clock_id;
    filter_size_ = config.filter_size;
    is_blocked_ = config.is_blocked != 0;
    return header_->magic == kShmMagic && header_->capacity == config.ring_capacity;
}

bool HookWriter::EnsureConnectedWithWriterLock()
{
    HotpathProfileMutexGuard lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    const bool ret = EnsureConnectedLocked();
    return ret;
}

bool HookWriter::HasConnectionWithWriterLock()
{
    HotpathProfileMutexGuard lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    const bool ret = header_ != nullptr;
    return ret;
}

bool HookWriter::ShouldRecordAllocLocked(size_t size)
{
    HotpathProfileScope profile_scope(HotpathProfileSegment::kSampleFilter);

    if (!PassesFilter(filter_size_, size)) {
        return false;
    }
    return ShouldSample(sample_interval_);
}

bool HookWriter::HasTrackedAllocLocked(uint64_t addr) const
{
    const uint64_t lookup_start = HotpathProfileStart();
    const bool ret = tracked_allocations_.find(addr) != tracked_allocations_.end();
    HotpathProfileAdd(HotpathProfileSegment::kTrackingLookup, lookup_start);
    return ret;
}

bool HookWriter::ConsumeTrackedAllocLocked(uint64_t addr)
{
    const uint64_t lookup_start = HotpathProfileStart();
    const auto it = tracked_allocations_.find(addr);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingLookup, lookup_start);
    if (it == tracked_allocations_.end()) {
        return false;
    }
    const uint64_t erase_start = HotpathProfileStart();
    tracked_allocations_.erase(it);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingErase, erase_start);
    return true;
}

HookWriter::TrackingShard& HookWriter::TrackingShardFor(uint64_t addr)
{
    return tracking_shards_[TrackingShardIndex(addr)];
}

bool HookWriter::HasTrackedAllocSharded(uint64_t addr)
{
    TrackingShard& shard = TrackingShardFor(addr);
    HotpathProfileMutexGuard lock(
        &shard.mutex,
        HotpathProfileSegment::kTrackingShardMutexWait,
        HotpathProfileSegment::kTrackingShardMutexHold);
    const uint64_t lookup_start = HotpathProfileStart();
    const bool ret = shard.allocations.find(addr) != shard.allocations.end();
    HotpathProfileAdd(HotpathProfileSegment::kTrackingLookup, lookup_start);
    return ret;
}

bool HookWriter::ConsumeTrackedAllocSharded(uint64_t addr)
{
    TrackingShard& shard = TrackingShardFor(addr);
    HotpathProfileMutexGuard lock(
        &shard.mutex,
        HotpathProfileSegment::kTrackingShardMutexWait,
        HotpathProfileSegment::kTrackingShardMutexHold);
    const uint64_t lookup_start = HotpathProfileStart();
    const auto it = shard.allocations.find(addr);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingLookup, lookup_start);
    if (it == shard.allocations.end()) {
        return false;
    }
    const uint64_t erase_start = HotpathProfileStart();
    shard.allocations.erase(it);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingErase, erase_start);
    return true;
}

void HookWriter::InsertTrackedAllocSharded(uint64_t addr)
{
    TrackingShard& shard = TrackingShardFor(addr);
    HotpathProfileMutexGuard lock(
        &shard.mutex,
        HotpathProfileSegment::kTrackingShardMutexWait,
        HotpathProfileSegment::kTrackingShardMutexHold);
    const uint64_t insert_start = HotpathProfileStart();
    shard.allocations.insert(addr);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingInsert, insert_start);
}

HookWriter::OwnershipShard& HookWriter::OwnershipShardFor(uint64_t addr)
{
    return ownership_shards_[TrackingShardIndex(addr)];
}

void HookWriter::RecordOwnershipThreadLocal(uint64_t addr, uint64_t owner_id)
{
    OwnershipShard& shard = OwnershipShardFor(addr);
    HotpathProfileMutexGuard lock(
        &shard.mutex,
        HotpathProfileSegment::kTrackingShardMutexWait,
        HotpathProfileSegment::kTrackingShardMutexHold);
    const uint64_t insert_start = HotpathProfileStart();
    shard.ownership_by_addr[addr] = owner_id;
    HotpathProfileAdd(HotpathProfileSegment::kTrackingInsert, insert_start);
}

bool HookWriter::HasTrackedAllocOwnershipThreadLocal(uint64_t addr, uint64_t owner_id)
{
    OwnershipShard& shard = OwnershipShardFor(addr);
    HotpathProfileMutexGuard lock(
        &shard.mutex,
        HotpathProfileSegment::kTrackingShardMutexWait,
        HotpathProfileSegment::kTrackingShardMutexHold);
    const uint64_t lookup_start = HotpathProfileStart();
    const auto it = shard.ownership_by_addr.find(addr);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingLookup, lookup_start);
    if (it == shard.ownership_by_addr.end()) {
        return false;
    }
    return it->second != owner_id;
}

bool HookWriter::ConsumeTrackedAllocOwnershipThreadLocal(uint64_t addr, uint64_t owner_id)
{
    OwnershipShard& shard = OwnershipShardFor(addr);
    HotpathProfileMutexGuard lock(
        &shard.mutex,
        HotpathProfileSegment::kTrackingShardMutexWait,
        HotpathProfileSegment::kTrackingShardMutexHold);
    const uint64_t lookup_start = HotpathProfileStart();
    const auto it = shard.ownership_by_addr.find(addr);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingLookup, lookup_start);
    if (it == shard.ownership_by_addr.end()) {
        return false;
    }

    const bool cross_thread = it->second != owner_id;
    const uint64_t erase_start = HotpathProfileStart();
    shard.ownership_by_addr.erase(it);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingErase, erase_start);
    return cross_thread;
}

bool HookWriter::RecordTrackingAblationAllocLocked(uint64_t addr, size_t size, int sub_ablation_stage)
{
    const bool should_record = ShouldRecordAllocLocked(size);
    if (sub_ablation_stage == kTrackingSubStageSampleFilterOnly) {
        return should_record;
    }
    if (!should_record) {
        return false;
    }
    const uint64_t insert_start = HotpathProfileStart();
    tracked_allocations_.insert(addr);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingInsert, insert_start);
    return true;
}

bool HookWriter::RecordTrackingAblationFreeLocked(uint64_t addr, int sub_ablation_stage)
{
    if (sub_ablation_stage == kTrackingSubStageSampleFilterOnly ||
        sub_ablation_stage == kTrackingSubStageInsertOnly) {
        return true;
    }
    if (sub_ablation_stage == kTrackingSubStageLookupOnly) {
        return HasTrackedAllocLocked(addr);
    }
    return ConsumeTrackedAllocLocked(addr);
}

bool HookWriter::RecordTrackingAblationAllocSharded(uint64_t addr, size_t size, int sub_ablation_stage)
{
    const bool should_record = ShouldRecordAllocLocked(size);
    if (sub_ablation_stage == kTrackingSubStageSampleFilterOnly) {
        return should_record;
    }
    if (!should_record) {
        return false;
    }
    InsertTrackedAllocSharded(addr);
    return true;
}

bool HookWriter::RecordTrackingAblationFreeSharded(uint64_t addr, int sub_ablation_stage)
{
    if (sub_ablation_stage == kTrackingSubStageSampleFilterOnly ||
        sub_ablation_stage == kTrackingSubStageInsertOnly) {
        return true;
    }
    if (sub_ablation_stage == kTrackingSubStageLookupOnly) {
        return HasTrackedAllocSharded(addr);
    }
    return ConsumeTrackedAllocSharded(addr);
}

bool HookWriter::HasTrackedAllocThreadLocal(bool use_fallback, uint64_t addr)
{
    ThreadTrackingContext& context = ThreadTracking();
    const uint64_t lookup_start = HotpathProfileStart();
    const bool local_hit = context.allocations.find(addr) != context.allocations.end();
    HotpathProfileAdd(HotpathProfileSegment::kTrackingLookup, lookup_start);
    if (local_hit) {
        return true;
    }
    return use_fallback ? HasTrackedAllocOwnershipThreadLocal(addr, ThreadTrackingOwnerId(context)) : false;
}

bool HookWriter::ConsumeTrackedAllocThreadLocal(bool use_fallback, uint64_t addr)
{
    ThreadTrackingContext& context = ThreadTracking();
    const uint64_t lookup_start = HotpathProfileStart();
    const auto local_it = context.allocations.find(addr);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingLookup, lookup_start);
    if (local_it != context.allocations.end()) {
        const uint64_t erase_start = HotpathProfileStart();
        context.allocations.erase(local_it);
        HotpathProfileAdd(HotpathProfileSegment::kTrackingErase, erase_start);
        // Miss-fallback: same-thread frees stay entirely local; shared ownership is checked only on local miss.
        return true;
    }
    return use_fallback ? ConsumeTrackedAllocOwnershipThreadLocal(addr, ThreadTrackingOwnerId(context)) : false;
}

void HookWriter::InsertTrackedAllocThreadLocal(bool use_fallback, uint64_t addr)
{
    ThreadTrackingContext& context = ThreadTracking();
    const uint64_t insert_start = HotpathProfileStart();
    context.allocations.insert(addr);
    HotpathProfileAdd(HotpathProfileSegment::kTrackingInsert, insert_start);
    if (use_fallback) {
        RecordOwnershipThreadLocal(addr, ThreadTrackingOwnerId(context));
    }
}

bool HookWriter::RecordTrackingAblationAllocThreadLocal(
    bool use_fallback, uint64_t addr, size_t size, int sub_ablation_stage)
{
    const bool should_record = ShouldRecordAllocLocked(size);
    if (sub_ablation_stage == kTrackingSubStageSampleFilterOnly) {
        return should_record;
    }
    if (!should_record) {
        return false;
    }
    InsertTrackedAllocThreadLocal(use_fallback, addr);
    return true;
}

bool HookWriter::RecordTrackingAblationFreeThreadLocal(bool use_fallback, uint64_t addr, int sub_ablation_stage)
{
    if (sub_ablation_stage == kTrackingSubStageSampleFilterOnly ||
        sub_ablation_stage == kTrackingSubStageInsertOnly) {
        return true;
    }
    if (sub_ablation_stage == kTrackingSubStageLookupOnly) {
        return HasTrackedAllocThreadLocal(use_fallback, addr);
    }
    return ConsumeTrackedAllocThreadLocal(use_fallback, addr);
}

void HookWriter::FillRecordForSubAblationLocked(
    HookRecord* record, HookEventType type, uint64_t addr, uint64_t size, int sub_ablation_stage)
{
    if (record == nullptr) {
        return;
    }

    const uint64_t fill_start = HotpathProfileStart();
    record->type = static_cast<uint16_t>(type);
    record->addr = addr;
    record->size = size;
    HotpathProfileAdd(HotpathProfileSegment::kRecordFill, fill_start);
    if (IncludesClockMetadata(sub_ablation_stage)) {
        record->ts = NowTs(clock_id_);
    }
    if (IncludesSyscallPidTidMetadata(sub_ablation_stage)) {
        record->pid = CurrentPid();
        record->tid = CurrentTid();
    } else if (sub_ablation_stage == kRecordWriteSubStageMetadataPidOnly) {
        record->pid = CurrentPid();
    } else if (sub_ablation_stage == kRecordWriteSubStageMetadataTidSyscallOnly) {
        record->tid = CurrentTid();
    } else if (sub_ablation_stage == kRecordWriteSubStageMetadataPidTidSyscall) {
        record->pid = CurrentPid();
        record->tid = CurrentTid();
    } else if (sub_ablation_stage == kRecordWriteSubStageMetadataCachedPidOnly) {
        record->pid = CachedPid();
    } else if (sub_ablation_stage == kRecordWriteSubStageMetadataThreadLocalTidOnly) {
        record->tid = ThreadLocalTid();
    } else if (sub_ablation_stage == kRecordWriteSubStageMetadataCachedPidThreadLocalTid) {
        record->pid = CachedPid();
        record->tid = ThreadLocalTid();
    } else if (sub_ablation_stage >= kRecordWriteSubStageCachedThreadNameNoRing &&
        sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain) {
        record->pid = CachedPid();
        record->tid = ThreadLocalTid();
    } else if (sub_ablation_stage >= kWriterRingSubStageRecordFillCached &&
        sub_ablation_stage <= kWriterRingSubStageAtomicIndexSelfDrain) {
        record->pid = CachedPid();
        record->tid = ThreadLocalTid();
    }
}

bool HookWriter::WriteRecordSubAblationLocked(const HookRecord& record, int sub_ablation_stage)
{
    if (!IncludesRingWritePath(sub_ablation_stage)) {
        return true;
    }
    if (header_ == nullptr || records_ == nullptr) {
        return false;
    }

    const uint64_t ring_start = HotpathProfileStart();
    const uint32_t capacity = header_->capacity;
    const uint32_t write_index = AtomicLoadU32(&header_->write_index);
    const uint32_t read_index = AtomicLoadU32(&header_->read_index);
    const uint32_t next_write = (write_index + 1) % capacity;
    if (next_write == read_index) {
        HotpathProfileAdd(HotpathProfileSegment::kRingIndexCheck, ring_start);
        AtomicFetchAddU32(&header_->dropped, 1);
        return false;
    }
    HotpathProfileAdd(HotpathProfileSegment::kRingIndexCheck, ring_start);
    if (IsRingIndexCheckOnlyStage(sub_ablation_stage)) {
        return true;
    }

    const uint64_t copy_start = HotpathProfileStart();
    records_[write_index] = record;
    HotpathProfileAdd(HotpathProfileSegment::kShmRecordCopy, copy_start);
    if (IsShmRecordCopyOnlyStage(sub_ablation_stage)) {
        return true;
    }

    const uint64_t atomic_start = HotpathProfileStart();
    AtomicStoreU32(&header_->write_index, next_write);
    ++pending_count_;
    AtomicStoreU32(&header_->read_index, next_write);
    pending_count_ = 0;
    HotpathProfileAdd(HotpathProfileSegment::kAtomicIndexUpdate, atomic_start);
    return true;
}

void HookWriter::MaybeWriteThreadNameSubAblationLocked(int sub_ablation_stage)
{
    if (!IncludesThreadNameMetadata(sub_ablation_stage)) {
        return;
    }

    if (g_thread_name_counter == 0) {
        HookRecord name_record {};
        FillThreadNameRecord(&name_record, clock_id_, UsesCachedPidTidMetadata(sub_ablation_stage));
        WriteRecordSubAblationLocked(name_record, sub_ablation_stage);
    }
    g_thread_name_counter = (g_thread_name_counter == kThreadNameRefreshInterval) ? 0 : (g_thread_name_counter + 1);
}

bool HookWriter::RecordWriteSubAblationAllocLocked(void* ptr, size_t size, int sub_ablation_stage)
{
    if (!ShouldRecordAllocLocked(size)) {
        return false;
    }
    if (sub_ablation_stage == kWriterRingSubStageWriterLockOnly) {
        tracked_allocations_.insert(reinterpret_cast<uint64_t>(ptr));
        return true;
    }

    MaybeWriteThreadNameSubAblationLocked(sub_ablation_stage);

    HookRecord record {};
    FillRecordForSubAblationLocked(
        &record,
        HookEventType::kMalloc,
        reinterpret_cast<uint64_t>(ptr),
        static_cast<uint64_t>(size),
        sub_ablation_stage);
    const bool ret = WriteRecordSubAblationLocked(record, sub_ablation_stage);
    if (ret) {
        tracked_allocations_.insert(reinterpret_cast<uint64_t>(ptr));
    }
    return ret;
}

bool HookWriter::RecordWriteSubAblationFreeLocked(void* ptr, int sub_ablation_stage)
{
    const uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    if (header_ == nullptr || !ConsumeTrackedAllocLocked(addr)) {
        return false;
    }
    if (sub_ablation_stage == kWriterRingSubStageWriterLockOnly) {
        return true;
    }

    MaybeWriteThreadNameSubAblationLocked(sub_ablation_stage);

    HookRecord record {};
    FillRecordForSubAblationLocked(&record, HookEventType::kFree, addr, 0, sub_ablation_stage);
    return WriteRecordSubAblationLocked(record, sub_ablation_stage);
}

bool HookWriter::RecordWriteSubAblationAllocSharded(void* ptr, size_t size, int sub_ablation_stage)
{
    if (!ShouldRecordAllocLocked(size)) {
        return false;
    }

    if (sub_ablation_stage == kWriterRingSubStageWriterLockOnly) {
        HotpathProfileMutexGuard writer_lock(
            &mutex_,
            HotpathProfileSegment::kWriterMutexWait,
            HotpathProfileSegment::kWriterMutexHold);
        writer_lock.Unlock();
        InsertTrackedAllocSharded(reinterpret_cast<uint64_t>(ptr));
        return true;
    }

    HookRecord record {};
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    MaybeWriteThreadNameSubAblationLocked(sub_ablation_stage);
    FillRecordForSubAblationLocked(
        &record,
        HookEventType::kMalloc,
        reinterpret_cast<uint64_t>(ptr),
        static_cast<uint64_t>(size),
        sub_ablation_stage);
    const bool ret = WriteRecordSubAblationLocked(record, sub_ablation_stage);
    writer_lock.Unlock();
    if (ret) {
        InsertTrackedAllocSharded(reinterpret_cast<uint64_t>(ptr));
    }
    return ret;
}

bool HookWriter::RecordWriteSubAblationFreeSharded(void* ptr, int sub_ablation_stage)
{
    if (!HasConnectionWithWriterLock()) {
        return false;
    }

    const uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    if (!ConsumeTrackedAllocSharded(addr)) {
        return false;
    }

    if (sub_ablation_stage == kWriterRingSubStageWriterLockOnly) {
        HotpathProfileMutexGuard writer_lock(
            &mutex_,
            HotpathProfileSegment::kWriterMutexWait,
            HotpathProfileSegment::kWriterMutexHold);
        return true;
    }

    HookRecord record {};
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    MaybeWriteThreadNameSubAblationLocked(sub_ablation_stage);
    FillRecordForSubAblationLocked(&record, HookEventType::kFree, addr, 0, sub_ablation_stage);
    const bool ret = WriteRecordSubAblationLocked(record, sub_ablation_stage);
    return ret;
}

bool HookWriter::RecordAllocSharded(void* ptr, size_t size, int ablation_stage)
{
    const uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    if (ablation_stage == kAblationStageTracking) {
        RecordTrackingAblationAllocSharded(addr, size, GetSubAblationStage());
        return true;
    }

    if (!EnsureConnectedWithWriterLock()) {
        return false;
    }

    const int sub_ablation_stage = GetSubAblationStage();
    if (ablation_stage == kAblationStageRecordWrite && IsRecordWriteSubAblationStage(sub_ablation_stage)) {
        return RecordWriteSubAblationAllocSharded(ptr, size, sub_ablation_stage);
    }

    if (!ShouldRecordAllocLocked(size)) {
        return false;
    }

    HookRecord record {};
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    MaybeWriteThreadNameLocked(ablation_stage);
    const uint64_t fill_start = HotpathProfileStart();
    record.type = static_cast<uint16_t>(HookEventType::kMalloc);
    record.addr = addr;
    record.size = static_cast<uint64_t>(size);
    HotpathProfileAdd(HotpathProfileSegment::kRecordFill, fill_start);
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    record.pid = MetadataPid(use_pid_tid_cache);
    record.tid = MetadataTid(use_pid_tid_cache);
    record.ts = NowTs(clock_id_);
    const bool ret = WriteRecordLocked(
        record,
        ablation_stage >= kAblationStageNotify,
        ablation_stage == kAblationStageRecordWrite);
    writer_lock.Unlock();
    if (ret) {
        InsertTrackedAllocSharded(record.addr);
    }
    return ret;
}

bool HookWriter::RecordFreeSharded(void* ptr, int ablation_stage)
{
    const uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    if (ablation_stage == kAblationStageTracking) {
        return RecordTrackingAblationFreeSharded(addr, GetSubAblationStage());
    }

    const int sub_ablation_stage = GetSubAblationStage();
    if (ablation_stage == kAblationStageRecordWrite && IsRecordWriteSubAblationStage(sub_ablation_stage)) {
        return RecordWriteSubAblationFreeSharded(ptr, sub_ablation_stage);
    }

    if (!HasConnectionWithWriterLock() || !ConsumeTrackedAllocSharded(addr)) {
        return false;
    }

    HookRecord record {};
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    MaybeWriteThreadNameLocked(ablation_stage);
    const uint64_t fill_start = HotpathProfileStart();
    record.type = static_cast<uint16_t>(HookEventType::kFree);
    record.addr = addr;
    record.size = 0;
    HotpathProfileAdd(HotpathProfileSegment::kRecordFill, fill_start);
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    record.pid = MetadataPid(use_pid_tid_cache);
    record.tid = MetadataTid(use_pid_tid_cache);
    record.ts = NowTs(clock_id_);
    const bool ret = WriteRecordLocked(
        record,
        ablation_stage >= kAblationStageNotify,
        ablation_stage == kAblationStageRecordWrite);
    return ret;
}

bool HookWriter::RecordWriteSubAblationAllocThreadLocal(
    bool use_fallback, void* ptr, size_t size, int sub_ablation_stage)
{
    if (!ShouldRecordAllocLocked(size)) {
        return false;
    }

    if (sub_ablation_stage == kWriterRingSubStageWriterLockOnly) {
        HotpathProfileMutexGuard writer_lock(
            &mutex_,
            HotpathProfileSegment::kWriterMutexWait,
            HotpathProfileSegment::kWriterMutexHold);
        writer_lock.Unlock();
        InsertTrackedAllocThreadLocal(use_fallback, reinterpret_cast<uint64_t>(ptr));
        return true;
    }

    HookRecord record {};
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    MaybeWriteThreadNameSubAblationLocked(sub_ablation_stage);
    FillRecordForSubAblationLocked(
        &record,
        HookEventType::kMalloc,
        reinterpret_cast<uint64_t>(ptr),
        static_cast<uint64_t>(size),
        sub_ablation_stage);
    const bool ret = WriteRecordSubAblationLocked(record, sub_ablation_stage);
    writer_lock.Unlock();
    if (ret) {
        InsertTrackedAllocThreadLocal(use_fallback, reinterpret_cast<uint64_t>(ptr));
    }
    return ret;
}

bool HookWriter::RecordWriteSubAblationFreeThreadLocal(bool use_fallback, void* ptr, int sub_ablation_stage)
{
    if (!HasConnectionWithWriterLock()) {
        return false;
    }

    const uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    if (!ConsumeTrackedAllocThreadLocal(use_fallback, addr)) {
        return false;
    }

    if (sub_ablation_stage == kWriterRingSubStageWriterLockOnly) {
        HotpathProfileMutexGuard writer_lock(
            &mutex_,
            HotpathProfileSegment::kWriterMutexWait,
            HotpathProfileSegment::kWriterMutexHold);
        return true;
    }

    HookRecord record {};
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    MaybeWriteThreadNameSubAblationLocked(sub_ablation_stage);
    FillRecordForSubAblationLocked(&record, HookEventType::kFree, addr, 0, sub_ablation_stage);
    const bool ret = WriteRecordSubAblationLocked(record, sub_ablation_stage);
    return ret;
}

bool HookWriter::RecordAllocThreadLocal(bool use_fallback, void* ptr, size_t size, int ablation_stage)
{
    const uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    if (ablation_stage == kAblationStageTracking) {
        RecordTrackingAblationAllocThreadLocal(use_fallback, addr, size, GetSubAblationStage());
        return true;
    }

    if (!EnsureConnectedWithWriterLock()) {
        return false;
    }

    const int sub_ablation_stage = GetSubAblationStage();
    if (ablation_stage == kAblationStageRecordWrite && IsRecordWriteSubAblationStage(sub_ablation_stage)) {
        return RecordWriteSubAblationAllocThreadLocal(use_fallback, ptr, size, sub_ablation_stage);
    }

    if (!ShouldRecordAllocLocked(size)) {
        return false;
    }

    HookRecord record {};
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    MaybeWriteThreadNameLocked(ablation_stage);
    const uint64_t fill_start = HotpathProfileStart();
    record.type = static_cast<uint16_t>(HookEventType::kMalloc);
    record.addr = addr;
    record.size = static_cast<uint64_t>(size);
    HotpathProfileAdd(HotpathProfileSegment::kRecordFill, fill_start);
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    record.pid = MetadataPid(use_pid_tid_cache);
    record.tid = MetadataTid(use_pid_tid_cache);
    record.ts = NowTs(clock_id_);
    const bool ret = WriteRecordLocked(
        record,
        ablation_stage >= kAblationStageNotify,
        ablation_stage == kAblationStageRecordWrite);
    writer_lock.Unlock();
    if (ret) {
        InsertTrackedAllocThreadLocal(use_fallback, record.addr);
    }
    return ret;
}

bool HookWriter::RecordFreeThreadLocal(bool use_fallback, void* ptr, int ablation_stage)
{
    const uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    if (ablation_stage == kAblationStageTracking) {
        return RecordTrackingAblationFreeThreadLocal(use_fallback, addr, GetSubAblationStage());
    }

    const int sub_ablation_stage = GetSubAblationStage();
    if (ablation_stage == kAblationStageRecordWrite && IsRecordWriteSubAblationStage(sub_ablation_stage)) {
        return RecordWriteSubAblationFreeThreadLocal(use_fallback, ptr, sub_ablation_stage);
    }

    if (!HasConnectionWithWriterLock() || !ConsumeTrackedAllocThreadLocal(use_fallback, addr)) {
        return false;
    }

    HookRecord record {};
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    MaybeWriteThreadNameLocked(ablation_stage);
    const uint64_t fill_start = HotpathProfileStart();
    record.type = static_cast<uint16_t>(HookEventType::kFree);
    record.addr = addr;
    record.size = 0;
    HotpathProfileAdd(HotpathProfileSegment::kRecordFill, fill_start);
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    record.pid = MetadataPid(use_pid_tid_cache);
    record.tid = MetadataTid(use_pid_tid_cache);
    record.ts = NowTs(clock_id_);
    const bool ret = WriteRecordLocked(
        record,
        ablation_stage >= kAblationStageNotify,
        ablation_stage == kAblationStageRecordWrite);
    return ret;
}

bool HookWriter::WriteRecordLocked(const HookRecord& record, bool allow_notify, bool self_drain)
{
    if (header_ == nullptr || records_ == nullptr) {
        return false;
    }

    const uint64_t ring_start = HotpathProfileStart();
    const uint32_t capacity = header_->capacity;
    const uint32_t write_index = AtomicLoadU32(&header_->write_index);
    const uint32_t read_index = AtomicLoadU32(&header_->read_index);
    const uint32_t next_write = (write_index + 1) % capacity;
    if (next_write == read_index) {
        HotpathProfileAdd(HotpathProfileSegment::kRingIndexCheck, ring_start);
        AtomicFetchAddU32(&header_->dropped, 1);
        return false;
    }
    HotpathProfileAdd(HotpathProfileSegment::kRingIndexCheck, ring_start);

    const uint64_t copy_start = HotpathProfileStart();
    records_[write_index] = record;
    HotpathProfileAdd(HotpathProfileSegment::kShmRecordCopy, copy_start);
    const uint64_t atomic_start = HotpathProfileStart();
    AtomicStoreU32(&header_->write_index, next_write);
    ++pending_count_;
    if (self_drain) {
        AtomicStoreU32(&header_->read_index, next_write);
        pending_count_ = 0;
        HotpathProfileAdd(HotpathProfileSegment::kAtomicIndexUpdate, atomic_start);
        return true;
    }
    HotpathProfileAdd(HotpathProfileSegment::kAtomicIndexUpdate, atomic_start);
    if (!allow_notify) {
        return true;
    }
    if (is_blocked_) {
        NotifyLocked();
        if (pending_count_ == 0) {
            WaitUntilDrainedLocked();
        }
    } else if (pending_count_ >= flush_threshold_) {
        NotifyLocked();
    }
    return true;
}

static void FillThreadNameRecord(HookRecord* record, int32_t clock_id, bool use_pid_tid_cache)
{
    if (record == nullptr) {
        return;
    }
    record->type = static_cast<uint16_t>(HookEventType::kThreadName);
    record->pid = MetadataPid(use_pid_tid_cache);
    record->tid = MetadataTid(use_pid_tid_cache);
    record->ts = NowTs(clock_id);
    const uint64_t thread_name_start = HotpathProfileStart();
    prctl(PR_GET_NAME, record->name);
    HotpathProfileAdd(HotpathProfileSegment::kThreadName, thread_name_start);
}

void HookWriter::MaybeWriteThreadNameLocked(int ablation_stage)
{
    if (g_thread_name_counter == 0) {
        HookRecord name_record {};
        FillThreadNameRecord(&name_record, clock_id_, GetPidTidCacheEnabled());
        WriteRecordLocked(
            name_record,
            ablation_stage >= kAblationStageNotify,
            ablation_stage == kAblationStageRecordWrite);
    }
    g_thread_name_counter = (g_thread_name_counter == kThreadNameRefreshInterval) ? 0 : (g_thread_name_counter + 1);
}

void HookWriter::WaitUntilDrainedLocked() const
{
    HotpathProfileScope profile_scope(HotpathProfileSegment::kWaitDrain);

    if (header_ == nullptr) {
        return;
    }

    while (AtomicLoadU32(&header_->read_index) != AtomicLoadU32(&header_->write_index)) {
        sched_yield();
    }
}

void HookWriter::NotifyLocked()
{
    HotpathProfileScope profile_scope(HotpathProfileSegment::kNotify);

    if (event_fd_ < 0 || pending_count_ == 0) {
        return;
    }
    const uint64_t one = 1;
    if (write(event_fd_, &one, sizeof(one)) == sizeof(one)) {
        pending_count_ = 0;
    }
}

bool HookWriter::RecordAlloc(void* ptr, size_t size)
{
    HotpathProfileScope profile_scope(HotpathProfileSegment::kRecordAllocTotal);

    if (ptr == nullptr) {
        return false;
    }

    const int ablation_stage = GetAblationStage();
    if (ablation_stage >= kAblationStageTracking) {
        const int tracking_mode = GetTrackingMode();
        if (tracking_mode == kTrackingModeSharded) {
            return RecordAllocSharded(ptr, size, ablation_stage);
        }
        if (tracking_mode == kTrackingModeThreadLocalFallback) {
            return RecordAllocThreadLocal(true, ptr, size, ablation_stage);
        }
        if (tracking_mode == kTrackingModeThreadLocalOnly) {
            return RecordAllocThreadLocal(false, ptr, size, ablation_stage);
        }
    }

    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    if (ablation_stage <= kAblationStageMutex) {
        return true;
    }
    if (ablation_stage == kAblationStageTracking) {
        RecordTrackingAblationAllocLocked(
            reinterpret_cast<uint64_t>(ptr), size, GetSubAblationStage());
        return true;
    }

    if (!EnsureConnectedLocked()) {
        return false;
    }
    const int sub_ablation_stage = GetSubAblationStage();
    if (ablation_stage == kAblationStageRecordWrite && IsRecordWriteSubAblationStage(sub_ablation_stage)) {
        const bool ret = RecordWriteSubAblationAllocLocked(ptr, size, sub_ablation_stage);
        return ret;
    }
    if (!ShouldRecordAllocLocked(size)) {
        return false;
    }

    MaybeWriteThreadNameLocked(ablation_stage);

    HookRecord record {};
    const uint64_t fill_start = HotpathProfileStart();
    record.type = static_cast<uint16_t>(HookEventType::kMalloc);
    record.addr = reinterpret_cast<uint64_t>(ptr);
    record.size = static_cast<uint64_t>(size);
    HotpathProfileAdd(HotpathProfileSegment::kRecordFill, fill_start);
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    record.pid = MetadataPid(use_pid_tid_cache);
    record.tid = MetadataTid(use_pid_tid_cache);
    record.ts = NowTs(clock_id_);
    const bool ret = WriteRecordLocked(
        record,
        ablation_stage >= kAblationStageNotify,
        ablation_stage == kAblationStageRecordWrite);
    if (ret) {
        const uint64_t insert_start = HotpathProfileStart();
        tracked_allocations_.insert(record.addr);
        HotpathProfileAdd(HotpathProfileSegment::kTrackingInsert, insert_start);
    }
    return ret;
}

bool HookWriter::RecordFree(void* ptr)
{
    HotpathProfileScope profile_scope(HotpathProfileSegment::kRecordFreeTotal);

    if (ptr == nullptr) {
        return false;
    }

    const int ablation_stage = GetAblationStage();
    if (ablation_stage >= kAblationStageTracking) {
        const int tracking_mode = GetTrackingMode();
        if (tracking_mode == kTrackingModeSharded) {
            return RecordFreeSharded(ptr, ablation_stage);
        }
        if (tracking_mode == kTrackingModeThreadLocalFallback) {
            return RecordFreeThreadLocal(true, ptr, ablation_stage);
        }
        if (tracking_mode == kTrackingModeThreadLocalOnly) {
            return RecordFreeThreadLocal(false, ptr, ablation_stage);
        }
    }

    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    if (ablation_stage <= kAblationStageMutex) {
        return true;
    }
    if (ablation_stage == kAblationStageTracking) {
        const bool ret = RecordTrackingAblationFreeLocked(
            reinterpret_cast<uint64_t>(ptr), GetSubAblationStage());
        return ret;
    }

    const int sub_ablation_stage = GetSubAblationStage();
    if (ablation_stage == kAblationStageRecordWrite && IsRecordWriteSubAblationStage(sub_ablation_stage)) {
        const bool ret = RecordWriteSubAblationFreeLocked(ptr, sub_ablation_stage);
        return ret;
    }

    if (header_ == nullptr || !ConsumeTrackedAllocLocked(reinterpret_cast<uint64_t>(ptr))) {
        return false;
    }

    MaybeWriteThreadNameLocked(ablation_stage);

    HookRecord record {};
    const uint64_t fill_start = HotpathProfileStart();
    record.type = static_cast<uint16_t>(HookEventType::kFree);
    record.addr = reinterpret_cast<uint64_t>(ptr);
    record.size = 0;
    HotpathProfileAdd(HotpathProfileSegment::kRecordFill, fill_start);
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    record.pid = MetadataPid(use_pid_tid_cache);
    record.tid = MetadataTid(use_pid_tid_cache);
    record.ts = NowTs(clock_id_);
    const bool ret = WriteRecordLocked(
        record,
        ablation_stage >= kAblationStageNotify,
        ablation_stage == kAblationStageRecordWrite);
    return ret;
}

void HookWriter::Flush()
{
    if (GetAblationStage() < kAblationStageNotify) {
        return;
    }
    HotpathProfileMutexGuard writer_lock(
        &mutex_,
        HotpathProfileSegment::kWriterMutexWait,
        HotpathProfileSegment::kWriterMutexHold);
    NotifyLocked();
}

}  // namespace linux_native_hook_v1
