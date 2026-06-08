#pragma once

#include <cstdint>
#include <string>

#include "common/shm_layout.h"

namespace linux_native_hook_v1 {

class Metrics;

class ShmConsumer {
public:
    ShmConsumer() = default;
    ~ShmConsumer();

    bool CreateAndMap(const std::string& shm_name, uint32_t capacity, uint32_t num_shards = 0);
    bool ConsumeAvailable(Metrics* metrics, bool verbose = false);

    int shm_fd() const { return shm_fd_; }
    uint32_t capacity() const { return header_ == nullptr ? 0 : header_->capacity; }

private:
    std::string shm_name_;
    int shm_fd_ = -1;
    size_t mapped_size_ = 0;
    void* mapping_ = nullptr;
    ShmHeader* header_ = nullptr;
    HookRecord* records_ = nullptr;
    uint32_t last_seen_dropped_ = 0;
    uint32_t last_seen_shard_dropped_[kShmMaxShards] = {};
};

}  // namespace linux_native_hook_v1
