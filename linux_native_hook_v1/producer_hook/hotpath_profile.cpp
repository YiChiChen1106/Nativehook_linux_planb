#include "producer_hook/hotpath_profile.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

#include <sys/syscall.h>

#include "producer_hook/ablation_config.h"

namespace linux_native_hook_v1 {
namespace {

constexpr size_t kSegmentCount = static_cast<size_t>(HotpathProfileSegment::kCount);

struct SegmentCounter {
    uint64_t count = 0;
    uint64_t total_cycles = 0;
};

struct ThreadProfileState {
    pid_t tid = 0;
    std::array<SegmentCounter, kSegmentCount> counters {};
    ThreadProfileState* next = nullptr;
};

pthread_mutex_t g_profile_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_once_t g_profile_atexit_once = PTHREAD_ONCE_INIT;
ThreadProfileState* g_profile_states = nullptr;
thread_local ThreadProfileState* g_thread_profile_state = nullptr;

const char* SegmentName(HotpathProfileSegment segment)
{
    switch (segment) {
    case HotpathProfileSegment::kHookEntry:
        return "hook_entry";
    case HotpathProfileSegment::kGuard:
        return "guard";
    case HotpathProfileSegment::kRecordAllocTotal:
        return "record_alloc_total";
    case HotpathProfileSegment::kRecordFreeTotal:
        return "record_free_total";
    case HotpathProfileSegment::kWriterMutexWait:
        return "writer_mutex_wait";
    case HotpathProfileSegment::kWriterMutexHold:
        return "writer_mutex_hold";
    case HotpathProfileSegment::kTrackingShardMutexWait:
        return "tracking_shard_mutex_wait";
    case HotpathProfileSegment::kTrackingShardMutexHold:
        return "tracking_shard_mutex_hold";
    case HotpathProfileSegment::kSampleFilter:
        return "sample_filter";
    case HotpathProfileSegment::kTrackingInsert:
        return "tracking_insert";
    case HotpathProfileSegment::kTrackingLookup:
        return "tracking_lookup";
    case HotpathProfileSegment::kTrackingErase:
        return "tracking_erase";
    case HotpathProfileSegment::kRecordFill:
        return "record_fill";
    case HotpathProfileSegment::kMetadataClock:
        return "metadata_clock";
    case HotpathProfileSegment::kMetadataPid:
        return "metadata_pid";
    case HotpathProfileSegment::kMetadataTid:
        return "metadata_tid";
    case HotpathProfileSegment::kThreadName:
        return "thread_name";
    case HotpathProfileSegment::kRingIndexCheck:
        return "ring_index_check";
    case HotpathProfileSegment::kShmRecordCopy:
        return "shm_record_copy";
    case HotpathProfileSegment::kAtomicIndexUpdate:
        return "atomic_index_update";
    case HotpathProfileSegment::kNotify:
        return "notify";
    case HotpathProfileSegment::kWaitDrain:
        return "wait_drain";
    case HotpathProfileSegment::kConnection:
        return "connection";
    case HotpathProfileSegment::kCount:
        return "count";
    }
    return "unknown";
}

uint64_t ReadCycles()
{
#if defined(__x86_64__) || defined(__i386__)
    unsigned int aux = 0;
    return __rdtscp(&aux);
#else
    timespec ts {};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
#endif
}

uint64_t TimespecToNs(const timespec& ts)
{
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

double EstimateNsPerCycle()
{
#if defined(__x86_64__) || defined(__i386__)
    timespec ts0 {};
    timespec ts1 {};
    timespec sleep_time {};
    sleep_time.tv_nsec = 20000000L;

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts0);
    const uint64_t c0 = ReadCycles();
    nanosleep(&sleep_time, nullptr);
    const uint64_t c1 = ReadCycles();
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts1);

    const uint64_t elapsed_ns = TimespecToNs(ts1) - TimespecToNs(ts0);
    const uint64_t elapsed_cycles = c1 - c0;
    if (elapsed_cycles == 0) {
        return 0.0;
    }
    return static_cast<double>(elapsed_ns) / static_cast<double>(elapsed_cycles);
#else
    return 1.0;
#endif
}

void WriteAll(int fd, const char* text)
{
    const size_t len = std::strlen(text);
    size_t written = 0;
    while (written < len) {
        const ssize_t ret = write(fd, text + written, len - written);
        if (ret <= 0) {
            return;
        }
        written += static_cast<size_t>(ret);
    }
}

void RegisterAtexit()
{
    std::atexit(HotpathProfileDumpIfEnabled);
}

ThreadProfileState* CurrentThreadProfile()
{
    pthread_once(&g_profile_atexit_once, RegisterAtexit);

    if (g_thread_profile_state != nullptr) {
        return g_thread_profile_state;
    }

    void* mapping = mmap(
        nullptr,
        sizeof(ThreadProfileState),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    if (mapping == MAP_FAILED) {
        return nullptr;
    }

    ThreadProfileState* state = static_cast<ThreadProfileState*>(mapping);
    state->tid = static_cast<pid_t>(syscall(SYS_gettid));

    pthread_mutex_lock(&g_profile_list_mutex);
    state->next = g_profile_states;
    g_profile_states = state;
    pthread_mutex_unlock(&g_profile_list_mutex);

    g_thread_profile_state = state;
    return state;
}

}  // namespace

bool HotpathProfileEnabled()
{
    return GetHotpathProfileEnabled();
}

uint64_t HotpathProfileStart()
{
    if (!HotpathProfileEnabled()) {
        return 0;
    }
    return ReadCycles();
}

void HotpathProfileAdd(HotpathProfileSegment segment, uint64_t start_cycles)
{
    if (start_cycles == 0 || !HotpathProfileEnabled()) {
        return;
    }

    const uint64_t end_cycles = ReadCycles();
    const size_t index = static_cast<size_t>(segment);
    if (index >= kSegmentCount) {
        return;
    }

    ThreadProfileState* state = CurrentThreadProfile();
    if (state == nullptr) {
        return;
    }

    SegmentCounter& counter = state->counters[index];
    ++counter.count;
    counter.total_cycles += end_cycles - start_cycles;
}

void HotpathProfileDumpIfEnabled()
{
    if (!HotpathProfileEnabled()) {
        return;
    }

    char default_path[256] {};
    std::snprintf(default_path, sizeof(default_path), "/tmp/lnhv1_hotpath_profile_%d.csv", static_cast<int>(getpid()));
    const char* path = std::getenv("LNHV1_HOTPATH_PROFILE_PATH");
    if (path == nullptr || path[0] == '\0') {
        path = default_path;
    }

    std::array<SegmentCounter, kSegmentCount> totals {};

    pthread_mutex_lock(&g_profile_list_mutex);
    for (ThreadProfileState* state = g_profile_states; state != nullptr; state = state->next) {
        for (size_t i = 0; i < kSegmentCount; ++i) {
            totals[i].count += state->counters[i].count;
            totals[i].total_cycles += state->counters[i].total_cycles;
        }
    }
    pthread_mutex_unlock(&g_profile_list_mutex);

    const double ns_per_cycle = EstimateNsPerCycle();
    const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }

    WriteAll(fd, "segment,count,total_cycles,avg_cycles,total_ns,avg_ns\n");
    for (size_t i = 0; i < kSegmentCount; ++i) {
        const SegmentCounter& counter = totals[i];
        if (counter.count == 0) {
            continue;
        }

        const double avg_cycles = static_cast<double>(counter.total_cycles) / static_cast<double>(counter.count);
        const double total_ns = static_cast<double>(counter.total_cycles) * ns_per_cycle;
        const double avg_ns = avg_cycles * ns_per_cycle;

        char line[256] {};
        std::snprintf(
            line,
            sizeof(line),
            "%s,%llu,%llu,%.2f,%.2f,%.2f\n",
            SegmentName(static_cast<HotpathProfileSegment>(i)),
            static_cast<unsigned long long>(counter.count),
            static_cast<unsigned long long>(counter.total_cycles),
            avg_cycles,
            total_ns,
            avg_ns);
        WriteAll(fd, line);
    }
    close(fd);
}

HotpathProfileMutexGuard::HotpathProfileMutexGuard(
    pthread_mutex_t* mutex,
    HotpathProfileSegment wait_segment,
    HotpathProfileSegment hold_segment)
    : mutex_(mutex)
    , hold_segment_(hold_segment)
{
    const uint64_t wait_start = HotpathProfileStart();
    pthread_mutex_lock(mutex_);
    HotpathProfileAdd(wait_segment, wait_start);
    hold_start_cycles_ = HotpathProfileStart();
    locked_ = true;
}

HotpathProfileMutexGuard::~HotpathProfileMutexGuard()
{
    Unlock();
}

void HotpathProfileMutexGuard::Unlock()
{
    if (!locked_) {
        return;
    }
    HotpathProfileAdd(hold_segment_, hold_start_cycles_);
    pthread_mutex_unlock(mutex_);
    locked_ = false;
}

}  // namespace linux_native_hook_v1
