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

namespace linux_native_hook_v1 {

namespace {

timespec NowTs(clockid_t clock_id)
{
    timespec ts {};
    clock_gettime(clock_id, &ts);
    return ts;
}

uint32_t CurrentPid()
{
    return static_cast<uint32_t>(getpid());
}

uint32_t CurrentTid()
{
    return static_cast<uint32_t>(syscall(SYS_gettid));
}

const char* ResolveSocketPath()
{
    const char* path = std::getenv("LNHV1_SOCKET_PATH");
    return (path != nullptr && path[0] != '\0') ? path : kDefaultSocketPath;
}

thread_local uint32_t g_thread_name_counter = 0;
thread_local uint32_t g_sample_counter = 0;
thread_local uint32_t g_cached_tid = 0;
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

bool IsRecordWriteSubAblationStage(int sub_ablation_stage)
{
    return (sub_ablation_stage >= kRecordWriteSubStageRecordFillMinimal &&
               sub_ablation_stage <= kRecordWriteSubStageAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kRecordWriteSubStageMetadataPidOnly &&
            sub_ablation_stage <= kRecordWriteSubStageMetadataCachedPidThreadLocalTid) ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedThreadNameNoRing &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain);
}

bool IncludesClockMetadata(int sub_ablation_stage)
{
    return (sub_ablation_stage >= kRecordWriteSubStageMetadataClock &&
               sub_ablation_stage <= kRecordWriteSubStageAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kRecordWriteSubStageMetadataPidOnly &&
            sub_ablation_stage <= kRecordWriteSubStageMetadataCachedPidThreadLocalTid) ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedThreadNameNoRing &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain);
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
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain);
}

bool IncludesRingWritePath(int sub_ablation_stage)
{
    return (sub_ablation_stage >= kRecordWriteSubStageRingIndexCheck &&
               sub_ablation_stage <= kRecordWriteSubStageAtomicIndexSelfDrain) ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedRingIndexCheck &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain);
}

bool IsRingIndexCheckOnlyStage(int sub_ablation_stage)
{
    return sub_ablation_stage == kRecordWriteSubStageRingIndexCheck ||
        sub_ablation_stage == kRecordWriteSubStageCachedRingIndexCheck;
}

bool IsShmRecordCopyOnlyStage(int sub_ablation_stage)
{
    return sub_ablation_stage == kRecordWriteSubStageShmRecordCopy ||
        sub_ablation_stage == kRecordWriteSubStageCachedShmRecordCopy;
}

bool UsesCachedPidTidMetadata(int sub_ablation_stage)
{
    return sub_ablation_stage == kRecordWriteSubStageMetadataCachedPidThreadLocalTid ||
        (sub_ablation_stage >= kRecordWriteSubStageCachedThreadNameNoRing &&
            sub_ablation_stage <= kRecordWriteSubStageCachedAtomicIndexSelfDrain);
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

bool HookWriter::ShouldRecordAllocLocked(size_t size)
{
    if (!PassesFilter(filter_size_, size)) {
        return false;
    }
    return ShouldSample(sample_interval_);
}

bool HookWriter::HasTrackedAllocLocked(uint64_t addr) const
{
    return tracked_allocations_.find(addr) != tracked_allocations_.end();
}

bool HookWriter::ConsumeTrackedAllocLocked(uint64_t addr)
{
    const auto it = tracked_allocations_.find(addr);
    if (it == tracked_allocations_.end()) {
        return false;
    }
    tracked_allocations_.erase(it);
    return true;
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
    tracked_allocations_.insert(addr);
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

void HookWriter::FillRecordForSubAblationLocked(
    HookRecord* record, HookEventType type, uint64_t addr, uint64_t size, int sub_ablation_stage)
{
    if (record == nullptr) {
        return;
    }

    record->type = static_cast<uint16_t>(type);
    record->addr = addr;
    record->size = size;
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

    const uint32_t capacity = header_->capacity;
    const uint32_t write_index = AtomicLoadU32(&header_->write_index);
    const uint32_t read_index = AtomicLoadU32(&header_->read_index);
    const uint32_t next_write = (write_index + 1) % capacity;
    if (next_write == read_index) {
        AtomicFetchAddU32(&header_->dropped, 1);
        return false;
    }
    if (IsRingIndexCheckOnlyStage(sub_ablation_stage)) {
        return true;
    }

    records_[write_index] = record;
    if (IsShmRecordCopyOnlyStage(sub_ablation_stage)) {
        return true;
    }

    AtomicStoreU32(&header_->write_index, next_write);
    ++pending_count_;
    AtomicStoreU32(&header_->read_index, next_write);
    pending_count_ = 0;
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

    MaybeWriteThreadNameSubAblationLocked(sub_ablation_stage);

    HookRecord record {};
    FillRecordForSubAblationLocked(&record, HookEventType::kFree, addr, 0, sub_ablation_stage);
    return WriteRecordSubAblationLocked(record, sub_ablation_stage);
}

bool HookWriter::WriteRecordLocked(const HookRecord& record, bool allow_notify, bool self_drain)
{
    if (header_ == nullptr || records_ == nullptr) {
        return false;
    }

    const uint32_t capacity = header_->capacity;
    const uint32_t write_index = AtomicLoadU32(&header_->write_index);
    const uint32_t read_index = AtomicLoadU32(&header_->read_index);
    const uint32_t next_write = (write_index + 1) % capacity;
    if (next_write == read_index) {
        AtomicFetchAddU32(&header_->dropped, 1);
        return false;
    }

    records_[write_index] = record;
    AtomicStoreU32(&header_->write_index, next_write);
    ++pending_count_;
    if (self_drain) {
        AtomicStoreU32(&header_->read_index, next_write);
        pending_count_ = 0;
        return true;
    }
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
    prctl(PR_GET_NAME, record->name);
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
    if (header_ == nullptr) {
        return;
    }

    while (AtomicLoadU32(&header_->read_index) != AtomicLoadU32(&header_->write_index)) {
        sched_yield();
    }
}

void HookWriter::NotifyLocked()
{
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
    if (ptr == nullptr) {
        return false;
    }

    const int ablation_stage = GetAblationStage();
    pthread_mutex_lock(&mutex_);
    if (ablation_stage <= kAblationStageMutex) {
        pthread_mutex_unlock(&mutex_);
        return true;
    }
    if (ablation_stage == kAblationStageTracking) {
        RecordTrackingAblationAllocLocked(
            reinterpret_cast<uint64_t>(ptr), size, GetSubAblationStage());
        pthread_mutex_unlock(&mutex_);
        return true;
    }

    if (!EnsureConnectedLocked()) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }
    const int sub_ablation_stage = GetSubAblationStage();
    if (ablation_stage == kAblationStageRecordWrite && IsRecordWriteSubAblationStage(sub_ablation_stage)) {
        const bool ret = RecordWriteSubAblationAllocLocked(ptr, size, sub_ablation_stage);
        pthread_mutex_unlock(&mutex_);
        return ret;
    }
    if (!ShouldRecordAllocLocked(size)) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    MaybeWriteThreadNameLocked(ablation_stage);

    HookRecord record {};
    record.type = static_cast<uint16_t>(HookEventType::kMalloc);
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    record.pid = MetadataPid(use_pid_tid_cache);
    record.tid = MetadataTid(use_pid_tid_cache);
    record.ts = NowTs(clock_id_);
    record.addr = reinterpret_cast<uint64_t>(ptr);
    record.size = static_cast<uint64_t>(size);
    const bool ret = WriteRecordLocked(
        record,
        ablation_stage >= kAblationStageNotify,
        ablation_stage == kAblationStageRecordWrite);
    if (ret) {
        tracked_allocations_.insert(record.addr);
    }
    pthread_mutex_unlock(&mutex_);
    return ret;
}

bool HookWriter::RecordFree(void* ptr)
{
    if (ptr == nullptr) {
        return false;
    }

    const int ablation_stage = GetAblationStage();
    pthread_mutex_lock(&mutex_);
    if (ablation_stage <= kAblationStageMutex) {
        pthread_mutex_unlock(&mutex_);
        return true;
    }
    if (ablation_stage == kAblationStageTracking) {
        const bool ret = RecordTrackingAblationFreeLocked(
            reinterpret_cast<uint64_t>(ptr), GetSubAblationStage());
        pthread_mutex_unlock(&mutex_);
        return ret;
    }

    const int sub_ablation_stage = GetSubAblationStage();
    if (ablation_stage == kAblationStageRecordWrite && IsRecordWriteSubAblationStage(sub_ablation_stage)) {
        const bool ret = RecordWriteSubAblationFreeLocked(ptr, sub_ablation_stage);
        pthread_mutex_unlock(&mutex_);
        return ret;
    }

    if (header_ == nullptr || !ConsumeTrackedAllocLocked(reinterpret_cast<uint64_t>(ptr))) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    MaybeWriteThreadNameLocked(ablation_stage);

    HookRecord record {};
    record.type = static_cast<uint16_t>(HookEventType::kFree);
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    record.pid = MetadataPid(use_pid_tid_cache);
    record.tid = MetadataTid(use_pid_tid_cache);
    record.ts = NowTs(clock_id_);
    record.addr = reinterpret_cast<uint64_t>(ptr);
    record.size = 0;
    const bool ret = WriteRecordLocked(
        record,
        ablation_stage >= kAblationStageNotify,
        ablation_stage == kAblationStageRecordWrite);
    pthread_mutex_unlock(&mutex_);
    return ret;
}

void HookWriter::Flush()
{
    if (GetAblationStage() < kAblationStageNotify) {
        return;
    }
    pthread_mutex_lock(&mutex_);
    NotifyLocked();
    pthread_mutex_unlock(&mutex_);
}

}  // namespace linux_native_hook_v1
