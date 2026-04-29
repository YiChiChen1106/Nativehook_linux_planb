#include "producer_fake/fake_writer.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/eventfd.h>
#include <time.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "common/socket_fd.h"

namespace linux_native_hook_v1 {

namespace {

timespec NowTs(clockid_t clock_id)
{
    timespec ts {};
    clock_gettime(clock_id, &ts);
    return ts;
}

uint32_t CurrentPid()
{
    return static_cast<uint32_t>(getpid());
}

uint32_t CurrentTid()
{
    return static_cast<uint32_t>(syscall(SYS_gettid));
}

bool PassesFilter(int32_t filter_size, size_t size)
{
    return filter_size < 0 || size >= static_cast<size_t>(filter_size);
}

}  // namespace

FakeWriter::~FakeWriter()
{
    if (mapping_ != nullptr) {
        munmap(mapping_, mapped_size_);
    }
    if (control_fd_ >= 0) {
        close(control_fd_);
    }
    if (shm_fd_ >= 0) {
        close(shm_fd_);
    }
    if (event_fd_ >= 0) {
        close(event_fd_);
    }
}

bool FakeWriter::Connect(const std::string& socket_path)
{
    control_fd_ = ConnectUnixSocket(socket_path);
    if (control_fd_ < 0) {
        return false;
    }

    const int32_t pid = static_cast<int32_t>(CurrentPid());
    if (!SendBytes(control_fd_, &pid, sizeof(pid))) {
        return false;
    }

    ControlConfig config {};
    if (!RecvBytes(control_fd_, &config, sizeof(config))) {
        return false;
    }
    if (!RecvFd(control_fd_, &shm_fd_)) {
        return false;
    }
    if (!RecvFd(control_fd_, &event_fd_)) {
        return false;
    }

    mapped_size_ = ShmBytesForCapacity(config.ring_capacity);
    mapping_ = mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        return false;
    }

    header_ = GetShmHeader(mapping_);
    records_ = GetShmRecords(mapping_);
    sample_interval_ = config.sample_interval == 0 ? kDefaultSampleInterval : config.sample_interval;
    filter_size_ = config.filter_size;
    return header_->magic == kShmMagic && header_->capacity == config.ring_capacity;
}

bool FakeWriter::ShouldKeepAlloc(size_t size)
{
    if (!PassesFilter(filter_size_, size)) {
        return false;
    }
    if (sample_interval_ <= 1) {
        return true;
    }
    const bool keep = (sample_counter_ % sample_interval_) == 0;
    sample_counter_ = (sample_counter_ == UINT32_MAX) ? 0 : (sample_counter_ + 1);
    return keep;
}

bool FakeWriter::WriteRecord(const HookRecord& record)
{
    if (header_ == nullptr || records_ == nullptr) {
        return false;
    }

    const uint32_t capacity = header_->capacity;
    const uint32_t write_index = AtomicLoadU32(&header_->write_index);
    const uint32_t read_index = AtomicLoadU32(&header_->read_index);
    const uint32_t next_write = (write_index + 1) % capacity;
    if (next_write == read_index) {
        AtomicFetchAddU32(&header_->dropped, 1);
        return false;
    }

    records_[write_index] = record;
    AtomicStoreU32(&header_->write_index, next_write);
    return true;
}

bool FakeWriter::WriteBurst(uint32_t burst_count)
{
    if (header_ == nullptr || event_fd_ < 0) {
        return false;
    }

    bool wrote_any = false;
    const uint32_t pid = CurrentPid();
    const uint32_t tid = CurrentTid();

    for (uint32_t i = 0; i < burst_count; ++i) {
        const uint64_t fake_ptr = next_fake_ptr_;
        next_fake_ptr_ += 64;
        const uint64_t alloc_size = 32 + (i % 64);
        if (!ShouldKeepAlloc(static_cast<size_t>(alloc_size))) {
            continue;
        }

        if (!thread_name_sent_) {
            HookRecord name_record {};
            name_record.type = static_cast<uint16_t>(HookEventType::kThreadName);
            name_record.pid = pid;
            name_record.tid = tid;
            name_record.ts = NowTs(CLOCK_REALTIME);
            prctl(PR_GET_NAME, name_record.name);
            wrote_any = WriteRecord(name_record) || wrote_any;
            thread_name_sent_ = true;
        }

        HookRecord alloc_record {};
        alloc_record.type = static_cast<uint16_t>(HookEventType::kMalloc);
        alloc_record.pid = pid;
        alloc_record.tid = tid;
        alloc_record.ts = NowTs(CLOCK_REALTIME);
        alloc_record.addr = fake_ptr;
        alloc_record.size = alloc_size;
        wrote_any = WriteRecord(alloc_record) || wrote_any;

        HookRecord free_record {};
        free_record.type = static_cast<uint16_t>(HookEventType::kFree);
        free_record.pid = pid;
        free_record.tid = tid;
        free_record.ts = NowTs(CLOCK_REALTIME);
        free_record.addr = fake_ptr;
        free_record.size = 0;
        wrote_any = WriteRecord(free_record) || wrote_any;
    }

    if (!wrote_any) {
        return false;
    }

    const uint64_t one = 1;
    return write(event_fd_, &one, sizeof(one)) == sizeof(one);
}

}  // namespace linux_native_hook_v1
