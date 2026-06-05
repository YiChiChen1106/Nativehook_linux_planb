#include "common/stack_writer.h"

#include <cstdio>

#include <unistd.h>

namespace linux_native_hook_v1 {

bool StackWriter::Write(const HookRecord* records, uint32_t record_count)
{
    return Write(records, record_count, false);
}

bool StackWriter::Write(const HookRecord* records, uint32_t record_count, bool self_drain)
{
    pthread_mutex_lock(&inner_mutex_);
    const bool ret = WriteLocked(records, record_count, self_drain);
    pthread_mutex_unlock(&inner_mutex_);
    return ret;
}

bool StackWriter::WriteLocked(const HookRecord* records, uint32_t record_count, bool self_drain)
{
    if (record_count == 0) {
        return true;
    }
    if (header_ == nullptr || records_ == nullptr || records == nullptr) {
        return false;
    }

    const uint32_t capacity = header_->capacity;
    const uint32_t write_index = AtomicLoadU32(&header_->write_index);
    const uint32_t read_index = AtomicLoadU32(&header_->read_index);
    const uint32_t used = (write_index >= read_index)
        ? (write_index - read_index)
        : (capacity - read_index + write_index);
    const uint32_t available = (capacity > used) ? (capacity - used - 1) : 0;
    const uint32_t writable_count = (record_count <= available) ? record_count : available;
    if (writable_count == 0) {
        AtomicFetchAddU32(&header_->dropped, record_count);
        return false;
    }

    for (uint32_t i = 0; i < writable_count; ++i) {
        records_[(write_index + i) % capacity] = records[i];
    }
    const uint32_t next_write = (write_index + writable_count) % capacity;
    AtomicStoreU32(&header_->write_index, next_write);
    pending_count_ += writable_count;
    if (self_drain) {
        AtomicStoreU32(&header_->read_index, next_write);
        pending_count_ = 0;
        if (writable_count < record_count) {
            AtomicFetchAddU32(&header_->dropped, record_count - writable_count);
        }
        return writable_count == record_count;
    }
    if (writable_count < record_count) {
        AtomicFetchAddU32(&header_->dropped, record_count - writable_count);
    }

    return writable_count == record_count;
}

bool StackWriter::PrepareFlush()
{
    return pending_count_ >= flush_threshold_;
}

bool StackWriter::Flush()
{
    if (event_fd_ < 0 || pending_count_ == 0) {
        return false;
    }
    if (pending_count_ < flush_threshold_) {
        return false;
    }
    const uint64_t one = 1;
    if (write(event_fd_, &one, sizeof(one)) == sizeof(one)) {
        pending_count_ = 0;
        return true;
    }
    return false;
}

bool StackWriter::FlushEventFd()
{
    if (event_fd_ < 0) {
        return false;
    }
    if (pending_count_ < flush_threshold_) {
        return false;
    }
    const uint64_t one = 1;
    return write(event_fd_, &one, sizeof(one)) == sizeof(one);
}

bool StackWriter::FlushForced()
{
    if (event_fd_ < 0 || pending_count_ == 0) {
        return false;
    }
    const uint64_t one = 1;
    if (write(event_fd_, &one, sizeof(one)) == sizeof(one)) {
        pending_count_ = 0;
        return true;
    }
    return false;
}

}  // namespace linux_native_hook_v1
