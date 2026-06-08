#include "common/stack_writer.h"

#include <cstdio>
#include <cstring>

#include <sys/syscall.h>
#include <unistd.h>

#include "producer_hook/ablation_config.h"

namespace linux_native_hook_v1 {

bool StackWriter::Write(const HookRecord* records, uint32_t record_count)
{
    return Write(records, record_count, false);
}

bool StackWriter::Write(const HookRecord* records, uint32_t record_count, bool self_drain)
{
    if (GetLockFreeRingEnabled()) {
        return WriteLocked(records, record_count, self_drain);
    }
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

    const uint32_t num_shards = header_->num_shards;
    const uint32_t capacity = header_->capacity;
    const bool sharded = (num_shards > 0);

    // Sharded mode: select shard by TID, single-writer per shard, no lock.
    if (sharded) {
        static thread_local uint32_t cached_tid = 0;
        static thread_local uint32_t cached_shard = 0;
        if (cached_tid == 0) {
            cached_tid = static_cast<uint32_t>(syscall(SYS_gettid));
            cached_shard = cached_tid % num_shards;
        }
        const uint32_t shard_idx = cached_shard;
        const uint32_t shard_cap = ShardCapacity(capacity, num_shards);
        ShmShard& shard = GetShard(header_, shard_idx);

        const uint32_t shard_write = AtomicLoadU32(&shard.write_index);
        const uint32_t shard_read = AtomicLoadU32(&shard.read_index);
        const uint32_t shard_used = (shard_write >= shard_read)
            ? (shard_write - shard_read)
            : (shard_cap - shard_read + shard_write);
        const uint32_t shard_avail = (shard_cap > shard_used) ? (shard_cap - shard_used - 1) : 0;
        const uint32_t writable = (record_count <= shard_avail) ? record_count : shard_avail;
        if (writable == 0) {
            AtomicFetchAddU32(&shard.dropped, record_count);
            return false;
        }

        const uint32_t offset = ShardRecordOffset(shard_idx, shard_cap);
        for (uint32_t i = 0; i < writable; ++i) {
            records_[offset + ((shard_write + i) % shard_cap)] = records[i];
        }

        const uint32_t next_write = (shard_write + writable) % shard_cap;
        AtomicStoreU32(&shard.write_index, next_write);

        // In sharded mode, consumer drains all shards; we don't update read_index.
        // pending_count_ is not meaningful in sharded mode.
        return writable == record_count;
    }

    // Non-sharded path (original logic).
    const bool lock_free_ring = GetLockFreeRingEnabled();

    uint32_t write_index;
    if (lock_free_ring) {
        // CAS loop to claim space in the ring buffer.
        // Simulates per-CPU ringbuf: no sleeping mutex, just atomic contention.
        uint32_t read_index;
        uint32_t next_write;
        do {
            write_index = AtomicLoadU32(&header_->write_index);
            read_index = AtomicLoadU32(&header_->read_index);
            const uint32_t used = (write_index >= read_index)
                ? (write_index - read_index)
                : (capacity - read_index + write_index);
            const uint32_t available = (capacity > used) ? (capacity - used - 1) : 0;
            if (available < record_count) {
                AtomicFetchAddU32(&header_->dropped, record_count);
                return false;
            }
            next_write = (write_index + record_count) % capacity;
        } while (!__atomic_compare_exchange_n(&header_->write_index, &write_index,
                 next_write, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
    } else {
        write_index = AtomicLoadU32(&header_->write_index);
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
        // In mutex mode, we may write fewer than requested.
        record_count = writable_count;
    }

    for (uint32_t i = 0; i < record_count; ++i) {
        records_[(write_index + i) % capacity] = records[i];
    }

    const uint32_t lock_delay_ns = GetLockDelayNs();
    if (lock_delay_ns > 0) {
        volatile uint64_t delay = lock_delay_ns * 10;
        while (delay--) {
            __asm__ volatile("" ::: "memory");
        }
    }

    // In lock-free mode, write_index was already advanced by CAS.
    if (!lock_free_ring) {
        const uint32_t next_write = (write_index + record_count) % capacity;
        AtomicStoreU32(&header_->write_index, next_write);
    }

    pending_count_ += record_count;
    if (self_drain) {
        const uint32_t next_write = (write_index + record_count) % capacity;
        AtomicStoreU32(&header_->read_index, next_write);
        pending_count_ = 0;
    }

    return true;
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
