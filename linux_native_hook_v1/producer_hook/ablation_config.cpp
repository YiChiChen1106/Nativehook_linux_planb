#include "producer_hook/ablation_config.h"

#include <atomic>
#include <cstdlib>

namespace linux_native_hook_v1 {
namespace {

constexpr int kUnsetAblationStage = 0;
std::atomic<int> g_cached_ablation_stage {kUnsetAblationStage};

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

}  // namespace linux_native_hook_v1
