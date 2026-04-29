#pragma once

#include <ctime>
#include <cstdint>

namespace linux_native_hook_v1 {

enum class HookEventType : uint32_t {
    kMalloc = 0,
    kFree = 1,
    kThreadName = 8,
    kEnd = 12,
};

struct HookRecord {
    timespec ts {};
    uint64_t addr = 0;
    uint64_t size = 0;
    uint32_t pid = 0;
    uint32_t tid = 0;
    uint16_t type = 0;
    uint16_t tag_id = 0;
    char name[32] = {0};
};

}  // namespace linux_native_hook_v1
