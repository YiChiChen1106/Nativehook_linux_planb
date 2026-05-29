#pragma once

#include <cstddef>
#include <cstdint>

#include "common/hook_record.h"

namespace linux_native_hook_v1 {

constexpr uint32_t kShmMagic = 0x4E484B31;  // "NHK1"
constexpr uint32_t kShmVersion = 2;

// Keep producer-written and consumer-written cursors on distinct cache lines.
struct alignas(64) ShmHeader {
    uint32_t magic = kShmMagic;
    uint32_t version = kShmVersion;
    uint32_t capacity = 0;
    uint32_t record_size = sizeof(HookRecord);
    uint32_t header_padding[12] = {};

    alignas(64) uint32_t write_index = 0;
    uint32_t write_padding[15] = {};

    alignas(64) uint32_t read_index = 0;
    uint32_t read_padding[15] = {};

    alignas(64) uint32_t dropped = 0;
    uint32_t dropped_padding[15] = {};
};

static_assert(alignof(ShmHeader) == 64, "ShmHeader must stay cache-line aligned");
static_assert(offsetof(ShmHeader, write_index) == 64, "write_index must start on its own cache line");
static_assert(offsetof(ShmHeader, read_index) == 128, "read_index must start on its own cache line");
static_assert(offsetof(ShmHeader, dropped) == 192, "dropped must start on its own cache line");
static_assert(sizeof(ShmHeader) == 256, "ShmHeader size must stay stable");

inline size_t ShmBytesForCapacity(uint32_t capacity)
{
    return sizeof(ShmHeader) + static_cast<size_t>(capacity) * sizeof(HookRecord);
}

inline ShmHeader* GetShmHeader(void* mapping)
{
    return static_cast<ShmHeader*>(mapping);
}

inline const ShmHeader* GetShmHeader(const void* mapping)
{
    return static_cast<const ShmHeader*>(mapping);
}

inline HookRecord* GetShmRecords(void* mapping)
{
    return reinterpret_cast<HookRecord*>(static_cast<std::byte*>(mapping) + sizeof(ShmHeader));
}

inline const HookRecord* GetShmRecords(const void* mapping)
{
    return reinterpret_cast<const HookRecord*>(static_cast<const std::byte*>(mapping) + sizeof(ShmHeader));
}

inline uint32_t AtomicLoadU32(const uint32_t* value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

inline void AtomicStoreU32(uint32_t* value, uint32_t new_value)
{
    __atomic_store_n(value, new_value, __ATOMIC_RELEASE);
}

inline uint32_t AtomicFetchAddU32(uint32_t* value, uint32_t delta)
{
    return __atomic_fetch_add(value, delta, __ATOMIC_RELAXED);
}

}  // namespace linux_native_hook_v1
