#include "consumer/shm_consumer.h"

#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "consumer/metrics.h"

namespace linux_native_hook_v1 {

ShmConsumer::~ShmConsumer()
{
    if (mapping_ != nullptr) {
        munmap(mapping_, mapped_size_);
    }
    if (shm_fd_ >= 0) {
        close(shm_fd_);
    }
    if (!shm_name_.empty()) {
        shm_unlink(shm_name_.c_str());
    }
}

bool ShmConsumer::CreateAndMap(const std::string& shm_name, uint32_t capacity)
{
    shm_name_ = shm_name;
    mapped_size_ = ShmBytesForCapacity(capacity);
    shm_fd_ = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0600);
    if (shm_fd_ < 0) {
        return false;
    }
    if (ftruncate(shm_fd_, static_cast<off_t>(mapped_size_)) != 0) {
        return false;
    }

    mapping_ = mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        return false;
    }

    std::memset(mapping_, 0, mapped_size_);
    header_ = GetShmHeader(mapping_);
    records_ = GetShmRecords(mapping_);
    header_->magic = kShmMagic;
    header_->version = kShmVersion;
    header_->capacity = capacity;
    header_->record_size = sizeof(HookRecord);
    return true;
}

bool ShmConsumer::ConsumeAvailable(Metrics* metrics, bool verbose)
{
    if (header_ == nullptr || metrics == nullptr) {
        return false;
    }

    const uint32_t capacity = header_->capacity;
    uint32_t read_index = AtomicLoadU32(&header_->read_index);
    const uint32_t write_index = AtomicLoadU32(&header_->write_index);
    if (capacity == 0 || read_index == write_index) {
        return true;
    }

    uint64_t alloc_count = 0;
    uint64_t free_count = 0;
    uint64_t batch_count = 0;
    while (read_index != write_index) {
        const HookRecord& record = records_[read_index];
        if (record.type == static_cast<uint16_t>(HookEventType::kMalloc)) {
            ++alloc_count;
        } else if (record.type == static_cast<uint16_t>(HookEventType::kFree)) {
            ++free_count;
        }
        if (verbose) {
            std::printf("VERBOSE,%u,%u,%lu,%lu,%lu,%ld,%ld\n",
                static_cast<unsigned>(record.type),
                static_cast<unsigned>(record.tid),
                static_cast<unsigned long>(record.addr),
                static_cast<unsigned long>(record.size),
                static_cast<unsigned long>(record.pid),
                static_cast<long>(record.ts.tv_sec),
                static_cast<long>(record.ts.tv_nsec));
        }
        read_index = (read_index + 1) % capacity;
        ++batch_count;
    }

    AtomicStoreU32(&header_->read_index, read_index);

    const uint32_t dropped_now = AtomicLoadU32(&header_->dropped);
    const uint32_t dropped_delta = dropped_now - last_seen_dropped_;
    last_seen_dropped_ = dropped_now;
    metrics->OnBatch(alloc_count, free_count, batch_count, dropped_delta);
    return true;
}

}  // namespace linux_native_hook_v1
