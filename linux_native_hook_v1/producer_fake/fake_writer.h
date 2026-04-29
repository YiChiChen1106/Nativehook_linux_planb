#pragma once

#include <cstdint>
#include <string>

#include "common/shm_layout.h"
#include "common/unix_defs.h"

namespace linux_native_hook_v1 {

class FakeWriter {
public:
    FakeWriter() = default;
    ~FakeWriter();

    bool Connect(const std::string& socket_path);
    bool WriteBurst(uint32_t burst_count);

private:
    bool ShouldKeepAlloc(size_t size);
    bool WriteRecord(const HookRecord& record);

    int control_fd_ = -1;
    int shm_fd_ = -1;
    int event_fd_ = -1;
    void* mapping_ = nullptr;
    size_t mapped_size_ = 0;
    ShmHeader* header_ = nullptr;
    HookRecord* records_ = nullptr;
    uint64_t next_fake_ptr_ = 0x100000;
    bool thread_name_sent_ = false;
    uint32_t sample_interval_ = kDefaultSampleInterval;
    int32_t filter_size_ = kDefaultFilterSize;
    uint32_t sample_counter_ = 0;
};

}  // namespace linux_native_hook_v1
