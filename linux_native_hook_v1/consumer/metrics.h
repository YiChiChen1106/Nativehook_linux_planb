#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace linux_native_hook_v1 {

class Metrics {
public:
    void OnBatch(uint64_t alloc_count, uint64_t free_count, uint64_t batch_count, uint64_t dropped_delta);
    std::string Snapshot() const;

private:
    uint64_t alloc_count_ = 0;
    uint64_t free_count_ = 0;
    uint64_t thread_name_count_ = 0;
    uint64_t total_records_ = 0;
    uint64_t flush_count_ = 0;
    uint64_t dropped_count_ = 0;
};

}  // namespace linux_native_hook_v1
