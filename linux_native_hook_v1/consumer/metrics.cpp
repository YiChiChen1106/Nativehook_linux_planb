#include "consumer/metrics.h"

#include <sstream>

namespace linux_native_hook_v1 {

void Metrics::OnBatch(uint64_t alloc_count, uint64_t free_count, uint64_t batch_count, uint64_t dropped_delta)
{
    alloc_count_ += alloc_count;
    free_count_ += free_count;
    const uint64_t named = (batch_count >= alloc_count + free_count) ? (batch_count - alloc_count - free_count) : 0;
    thread_name_count_ += named;
    total_records_ += batch_count;
    dropped_count_ += dropped_delta;
    ++flush_count_;
}

std::string Metrics::Snapshot() const
{
    std::ostringstream oss;
    oss << "records=" << total_records_
        << " alloc=" << alloc_count_
        << " free=" << free_count_
        << " thread_name=" << thread_name_count_
        << " flush=" << flush_count_
        << " dropped=" << dropped_count_;
    if (flush_count_ > 0) {
        const double avg_batch = static_cast<double>(total_records_) / static_cast<double>(flush_count_);
        oss << " avg_batch=" << avg_batch;
    }
    return oss.str();
}

}  // namespace linux_native_hook_v1
