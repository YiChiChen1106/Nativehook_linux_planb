#include "producer_hook/stack_capture.h"

#include <algorithm>
#include <cstdlib>
#include <execinfo.h>

namespace linux_native_hook_v1 {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

bool EnvEnabled(const char* value)
{
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

uint16_t ConfiguredDepth()
{
    const char* value = std::getenv("LNHV1_MAX_STACK_DEPTH");
    if (value == nullptr || value[0] == '\0') {
        return kMaxStackFrames;
    }

    const long parsed = std::strtol(value, nullptr, 10);
    if (parsed <= 0) {
        return 0;
    }
    return static_cast<uint16_t>(std::min<long>(parsed, kMaxStackFrames));
}

uint32_t HashFrames(const uint64_t* frames, uint16_t depth)
{
    uint64_t hash = kFnvOffset;
    for (uint16_t i = 0; i < depth; ++i) {
        uint64_t value = frames[i];
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= static_cast<uint8_t>(value & 0xffU);
            hash *= kFnvPrime;
            value >>= 8U;
        }
    }

    const uint32_t stack_id = static_cast<uint32_t>((hash >> 32U) ^ (hash & 0xffffffffU));
    return stack_id == 0 ? 1 : stack_id;
}

}  // namespace

bool StackCaptureEnabled()
{
    static const bool enabled = EnvEnabled(std::getenv("LNHV1_STACK_CAPTURE"));
    return enabled;
}

CapturedStack CaptureStack()
{
    CapturedStack captured {};
    if (!StackCaptureEnabled()) {
        return captured;
    }

    const uint16_t max_depth = ConfiguredDepth();
    if (max_depth == 0) {
        return captured;
    }

    void* raw[kMaxStackFrames] = {nullptr};
    const int depth = backtrace(raw, max_depth);
    if (depth <= 0) {
        return captured;
    }

    captured.depth = static_cast<uint16_t>(depth);
    for (uint16_t i = 0; i < captured.depth; ++i) {
        captured.frames[i] = reinterpret_cast<uint64_t>(raw[i]);
    }
    captured.stack_id = HashFrames(captured.frames, captured.depth);
    return captured;
}

}  // namespace linux_native_hook_v1
