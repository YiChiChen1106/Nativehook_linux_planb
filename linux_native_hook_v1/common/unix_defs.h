#pragma once

#include <ctime>
#include <cstdint>

namespace linux_native_hook_v1 {

constexpr char kDefaultSocketPath[] = "/tmp/linux_native_hook_v1.sock";
constexpr char kDefaultShmName[] = "/linux_native_hook_v1_shm";
constexpr uint32_t kDefaultRingCapacity = 32768;
constexpr uint32_t kDefaultBurstCount = 20;
constexpr uint32_t kDefaultFlushThreshold = 20;
constexpr uint32_t kDefaultSampleInterval = 1;
constexpr int32_t kDefaultFilterSize = -1;
constexpr uint8_t kDefaultMaxStackDepth = 0;

struct ControlConfig {
    uint32_t ring_capacity = 0;
    uint32_t flush_threshold = kDefaultFlushThreshold;
    uint32_t sample_interval = kDefaultSampleInterval;
    int32_t clock_id = CLOCK_REALTIME;
    int32_t filter_size = kDefaultFilterSize;
    uint8_t max_stack_depth = kDefaultMaxStackDepth;
    uint8_t reserved0 = 0;
    uint8_t reserved1 = 0;
    uint8_t is_blocked = 0;
};

}  // namespace linux_native_hook_v1
