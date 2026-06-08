#pragma once

#include <cstddef>
#include <cstdint>

#include "common/hook_record.h"

namespace linux_native_hook_v1 {

constexpr uint32_t kShmMagic = 0x4E484B31;  // "NHK1"
constexpr uint32_t kShmVersion = 1;
constexpr uint32_t kShmMaxShards = 16;

struct ShmShard {
    uint32_t write_index = 0;
    uint32_t read_index = 0;
    uint32_t dropped = 0;
};

struct ShmHeader {
    uint32_t magic = kShmMagic;
    uint32_t version = kShmVersion;
    uint32_t capacity = 0;
    uint32_t record_size = sizeof(HookRecord);
    // In single-shard mode (num_shards == 0):
    //   write_index, read_index, dropped are global.
    // In sharded mode (num_shards > 0):
    //   shard_count shards follow the header; the legacy fields
    //   below serve as shard[0]'s indices.
    uint32_t num_shards = 0;
    uint32_t write_index = 0;
    uint32_t read_index = 0;
    uint32_t dropped = 0;
};

inline size_t ShmHeaderBytes(uint32_t num_shards)
{
    if (num_shards <= 1) {
        return sizeof(ShmHeader);
    }
    return sizeof(ShmHeader) + (num_shards - 1) * sizeof(ShmShard);
}

inline size_t ShmBytesForCapacity(uint32_t capacity, uint32_t num_shards = 0)
{
    return ShmHeaderBytes(num_shards) + static_cast<size_t>(capacity) * sizeof(HookRecord);
}

inline ShmHeader* GetShmHeader(void* mapping)
{
    return static_cast<ShmHeader*>(mapping);
}

inline const ShmHeader* GetShmHeader(const void* mapping)
{
    return static_cast<const ShmHeader*>(mapping);
}

inline ShmShard* GetShmShards(ShmHeader* header)
{
    return reinterpret_cast<ShmShard*>(header + 1);
}

inline const ShmShard* GetShmShards(const ShmHeader* header)
{
    return reinterpret_cast<const ShmShard*>(header + 1);
}

inline HookRecord* GetShmRecords(void* mapping, uint32_t num_shards = 0)
{
    return reinterpret_cast<HookRecord*>(
        static_cast<std::byte*>(mapping) + ShmHeaderBytes(num_shards));
}

inline const HookRecord* GetShmRecords(const void* mapping, uint32_t num_shards = 0)
{
    return reinterpret_cast<const HookRecord*>(
        static_cast<const std::byte*>(mapping) + ShmHeaderBytes(num_shards));
}

inline ShmShard& GetShard(ShmHeader* header, uint32_t shard_idx)
{
    if (shard_idx == 0) {
        // Shard 0 uses the legacy fields in ShmHeader.
        return *reinterpret_cast<ShmShard*>(&header->write_index);
    }
    return GetShmShards(header)[shard_idx - 1];
}

inline const ShmShard& GetShard(const ShmHeader* header, uint32_t shard_idx)
{
    if (shard_idx == 0) {
        return *reinterpret_cast<const ShmShard*>(&header->write_index);
    }
    return GetShmShards(header)[shard_idx - 1];
}

inline uint32_t ShardCapacity(uint32_t total_capacity, uint32_t num_shards)
{
    return total_capacity / num_shards;
}

inline uint32_t ShardRecordOffset(uint32_t shard_idx, uint32_t shard_capacity)
{
    return shard_idx * shard_capacity;
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
