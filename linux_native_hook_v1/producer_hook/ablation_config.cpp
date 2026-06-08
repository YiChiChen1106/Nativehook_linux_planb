#include "producer_hook/ablation_config.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "common/shm_layout.h"

namespace linux_native_hook_v1 {
namespace {

constexpr int kUnsetAblationStage = 0;
std::atomic<int> g_cached_ablation_stage {kUnsetAblationStage};
constexpr int kUnsetSubAblationStage = -1;
std::atomic<int> g_cached_sub_ablation_stage {kUnsetSubAblationStage};
constexpr int kUnsetPidTidCacheEnabled = -1;
std::atomic<int> g_cached_pid_tid_cache_enabled {kUnsetPidTidCacheEnabled};
constexpr int kUnsetTrackingMode = -1;
std::atomic<int> g_cached_tracking_mode {kUnsetTrackingMode};
constexpr int kUnsetHotpathProfileEnabled = -1;
std::atomic<int> g_cached_hotpath_profile_enabled {kUnsetHotpathProfileEnabled};
constexpr int kUnsetStage6BatchSize = -1;
std::atomic<int> g_cached_stage6_batch_size {kUnsetStage6BatchSize};
constexpr int kUnsetStackWriterBatchSize = -1;
std::atomic<int> g_cached_stack_writer_batch_size {kUnsetStackWriterBatchSize};

int ParseAblationStageFromEnv()
{
    const char* text = std::getenv("LNHV1_ABLATION_STAGE");
    if (text == nullptr || text[0] == '\0') {
        return kDefaultAblationStage;
    }

    char* end_ptr = nullptr;
    const long value = std::strtol(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' ||
        value < kAblationStageHookEntry || value > kAblationStageNotify) {
        return kDefaultAblationStage;
    }

    return static_cast<int>(value);
}

int ParseSubAblationStageFromEnv()
{
    const char* text = std::getenv("LNHV1_SUBABLATION_STAGE");
    if (text == nullptr || text[0] == '\0') {
        return kSubAblationStageDisabled;
    }

    char* end_ptr = nullptr;
    const long value = std::strtol(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' ||
        value < kSubAblationStageDisabled || value > kMaxSubAblationStage) {
        return kSubAblationStageDisabled;
    }

    return static_cast<int>(value);
}

bool ParsePidTidCacheEnabledFromEnv()
{
    const char* text = std::getenv("LNHV1_PID_TID_CACHE");
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    char* end_ptr = nullptr;
    const long value = std::strtol(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0') {
        return false;
    }

    return value == 1;
}

int ParseTrackingModeFromEnv()
{
    const char* text = std::getenv("LNHV1_TRACKING_MODE");
    if (text == nullptr || text[0] == '\0') {
        return kTrackingModeGlobal;
    }

    if (std::strcmp(text, "sharded") == 0) {
        return kTrackingModeSharded;
    }
    if (std::strcmp(text, "thread_local_fallback") == 0) {
        return kTrackingModeThreadLocalFallback;
    }
    if (std::strcmp(text, "thread_local_only") == 0) {
        return kTrackingModeThreadLocalOnly;
    }
    if (std::strcmp(text, "global") == 0) {
        return kTrackingModeGlobal;
    }
    return kTrackingModeGlobal;
}

bool ParseHotpathProfileEnabledFromEnv()
{
    const char* text = std::getenv("LNHV1_HOTPATH_PROFILE");
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    char* end_ptr = nullptr;
    const long value = std::strtol(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0') {
        return false;
    }

    return value == 1;
}

uint32_t ParseStage6BatchSizeFromEnv()
{
    const char* text = std::getenv("LNHV1_STAGE6_BATCH_SIZE");
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }

    char* end_ptr = nullptr;
    const unsigned long value = std::strtoul(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || value > kMaxStage6BatchSize) {
        return 0;
    }

    return static_cast<uint32_t>(value);
}

}  // namespace

int GetAblationStage()
{
    int stage = g_cached_ablation_stage.load(std::memory_order_acquire);
    if (stage != kUnsetAblationStage) {
        return stage;
    }

    const int parsed_stage = ParseAblationStageFromEnv();
    int expected = kUnsetAblationStage;
    g_cached_ablation_stage.compare_exchange_strong(
        expected, parsed_stage, std::memory_order_release, std::memory_order_relaxed);
    return g_cached_ablation_stage.load(std::memory_order_acquire);
}

int GetSubAblationStage()
{
    int stage = g_cached_sub_ablation_stage.load(std::memory_order_acquire);
    if (stage != kUnsetSubAblationStage) {
        return stage;
    }

    const int parsed_stage = ParseSubAblationStageFromEnv();
    int expected = kUnsetSubAblationStage;
    g_cached_sub_ablation_stage.compare_exchange_strong(
        expected, parsed_stage, std::memory_order_release, std::memory_order_relaxed);
    return g_cached_sub_ablation_stage.load(std::memory_order_acquire);
}

bool GetPidTidCacheEnabled()
{
    int enabled = g_cached_pid_tid_cache_enabled.load(std::memory_order_acquire);
    if (enabled != kUnsetPidTidCacheEnabled) {
        return enabled != 0;
    }

    const int parsed_enabled = ParsePidTidCacheEnabledFromEnv() ? 1 : 0;
    int expected = kUnsetPidTidCacheEnabled;
    g_cached_pid_tid_cache_enabled.compare_exchange_strong(
        expected, parsed_enabled, std::memory_order_release, std::memory_order_relaxed);
    return g_cached_pid_tid_cache_enabled.load(std::memory_order_acquire) != 0;
}

int GetTrackingMode()
{
    int mode = g_cached_tracking_mode.load(std::memory_order_acquire);
    if (mode != kUnsetTrackingMode) {
        return mode;
    }

    const int parsed_mode = ParseTrackingModeFromEnv();
    int expected = kUnsetTrackingMode;
    g_cached_tracking_mode.compare_exchange_strong(
        expected, parsed_mode, std::memory_order_release, std::memory_order_relaxed);
    return g_cached_tracking_mode.load(std::memory_order_acquire);
}

bool GetHotpathProfileEnabled()
{
    int enabled = g_cached_hotpath_profile_enabled.load(std::memory_order_acquire);
    if (enabled != kUnsetHotpathProfileEnabled) {
        return enabled != 0;
    }

    const int parsed_enabled = ParseHotpathProfileEnabledFromEnv() ? 1 : 0;
    int expected = kUnsetHotpathProfileEnabled;
    g_cached_hotpath_profile_enabled.compare_exchange_strong(
        expected, parsed_enabled, std::memory_order_release, std::memory_order_relaxed);
    return g_cached_hotpath_profile_enabled.load(std::memory_order_acquire) != 0;
}

uint32_t GetStage6BatchSize()
{
    int batch_size = g_cached_stage6_batch_size.load(std::memory_order_acquire);
    if (batch_size != kUnsetStage6BatchSize) {
        return static_cast<uint32_t>(batch_size);
    }

    const int parsed_batch_size = static_cast<int>(ParseStage6BatchSizeFromEnv());
    int expected = kUnsetStage6BatchSize;
    g_cached_stage6_batch_size.compare_exchange_strong(
        expected, parsed_batch_size, std::memory_order_release, std::memory_order_relaxed);
    return static_cast<uint32_t>(g_cached_stage6_batch_size.load(std::memory_order_acquire));
}

uint32_t ParseStackWriterBatchSizeFromEnv()
{
    const char* text = std::getenv("LNHV1_STACK_WRITER_BATCH_SIZE");
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }

    char* end_ptr = nullptr;
    const unsigned long value = std::strtoul(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || value > kMaxStage6BatchSize) {
        return 0;
    }

    return static_cast<uint32_t>(value);
}

uint32_t GetStackWriterBatchSize()
{
    int batch_size = g_cached_stack_writer_batch_size.load(std::memory_order_acquire);
    if (batch_size != kUnsetStackWriterBatchSize) {
        return static_cast<uint32_t>(batch_size);
    }

    const int parsed_batch_size = static_cast<int>(ParseStackWriterBatchSizeFromEnv());
    int expected = kUnsetStackWriterBatchSize;
    g_cached_stack_writer_batch_size.compare_exchange_strong(
        expected, parsed_batch_size, std::memory_order_release, std::memory_order_relaxed);
    return static_cast<uint32_t>(g_cached_stack_writer_batch_size.load(std::memory_order_acquire));
}

constexpr int kUnsetLockDelayNs = -1;
std::atomic<int> g_cached_lock_delay_ns {kUnsetLockDelayNs};

uint32_t ParseLockDelayNsFromEnv()
{
    const char* text = std::getenv("LNHV1_LOCK_DELAY_NS");
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }

    char* end_ptr = nullptr;
    const unsigned long value = std::strtoul(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || value > 1000000) {
        return 0;
    }

    return static_cast<uint32_t>(value);
}

uint32_t GetLockDelayNs()
{
    int delay = g_cached_lock_delay_ns.load(std::memory_order_acquire);
    if (delay != kUnsetLockDelayNs) {
        return static_cast<uint32_t>(delay);
    }

    const int parsed_delay = static_cast<int>(ParseLockDelayNsFromEnv());
    int expected = kUnsetLockDelayNs;
    g_cached_lock_delay_ns.compare_exchange_strong(
        expected, parsed_delay, std::memory_order_release, std::memory_order_relaxed);
    return static_cast<uint32_t>(g_cached_lock_delay_ns.load(std::memory_order_acquire));
}

constexpr int kUnsetClientLockEnabled = -1;
std::atomic<int> g_cached_client_lock_enabled {kUnsetClientLockEnabled};

bool ParseClientLockEnabledFromEnv()
{
    const char* text = std::getenv("LNHV1_CLIENT_LOCK");
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    char* end_ptr = nullptr;
    const long value = std::strtol(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0') {
        return false;
    }

    return value == 1;
}

bool GetClientLockEnabled()
{
    int enabled = g_cached_client_lock_enabled.load(std::memory_order_acquire);
    if (enabled != kUnsetClientLockEnabled) {
        return enabled != 0;
    }

    const int parsed_enabled = ParseClientLockEnabledFromEnv() ? 1 : 0;
    int expected = kUnsetClientLockEnabled;
    g_cached_client_lock_enabled.compare_exchange_strong(
        expected, parsed_enabled, std::memory_order_release, std::memory_order_relaxed);
    return g_cached_client_lock_enabled.load(std::memory_order_acquire) != 0;
}

constexpr int kUnsetLockFreeRingEnabled = -1;
std::atomic<int> g_cached_lock_free_ring_enabled {kUnsetLockFreeRingEnabled};

bool ParseLockFreeRingEnabledFromEnv()
{
    const char* text = std::getenv("LNHV1_LOCK_FREE_RING");
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    char* end_ptr = nullptr;
    const long value = std::strtol(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0') {
        return false;
    }

    return value == 1;
}

bool GetLockFreeRingEnabled()
{
    int enabled = g_cached_lock_free_ring_enabled.load(std::memory_order_acquire);
    if (enabled != kUnsetLockFreeRingEnabled) {
        return enabled != 0;
    }

    const int parsed_enabled = ParseLockFreeRingEnabledFromEnv() ? 1 : 0;
    int expected = kUnsetLockFreeRingEnabled;
    g_cached_lock_free_ring_enabled.compare_exchange_strong(
        expected, parsed_enabled, std::memory_order_release, std::memory_order_relaxed);
    return g_cached_lock_free_ring_enabled.load(std::memory_order_acquire) != 0;
}

constexpr int kUnsetShardedRingShards = -1;
std::atomic<int> g_cached_sharded_ring_shards {kUnsetShardedRingShards};

uint32_t ParseShardedRingShardsFromEnv()
{
    const char* text = std::getenv("LNHV1_SHARDED_RING");
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }

    char* end_ptr = nullptr;
    const unsigned long value = std::strtoul(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || value > kShmMaxShards) {
        return 0;
    }

    return static_cast<uint32_t>(value);
}

uint32_t GetShardedRingShards()
{
    int shards = g_cached_sharded_ring_shards.load(std::memory_order_acquire);
    if (shards != kUnsetShardedRingShards) {
        return static_cast<uint32_t>(shards);
    }

    const int parsed_shards = static_cast<int>(ParseShardedRingShardsFromEnv());
    int expected = kUnsetShardedRingShards;
    g_cached_sharded_ring_shards.compare_exchange_strong(
        expected, parsed_shards, std::memory_order_release, std::memory_order_relaxed);
    return static_cast<uint32_t>(g_cached_sharded_ring_shards.load(std::memory_order_acquire));
}

}  // namespace linux_native_hook_v1
