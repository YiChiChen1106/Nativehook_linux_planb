#include "producer_hook/ablation_config.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

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
    if (std::strcmp(text, "global") == 0) {
        return kTrackingModeGlobal;
    }
    return kTrackingModeGlobal;
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

}  // namespace linux_native_hook_v1
