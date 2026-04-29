#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

enum ModeFlags : uint32_t {
    MODE_BASELINE  = 0,
    MODE_TIMESTAMP = 1U << 0,
    MODE_ATOMIC    = 1U << 1,
    MODE_STACK     = 1U << 2,
    MODE_BUFFER    = 1U << 3,
};

struct Config {
    int thread_count = 1;
    int duration_seconds = 60;
    int alloc_size = 19;
    int stack_depth = 8;
    int record_size = 64;
    uint32_t mode_flags = MODE_BASELINE;
    std::string output_path;
    std::string mode_name = "baseline";
};

struct ThreadResult {
    uint64_t iterations = 0;
};

std::atomic<uint64_t> g_shared_counter {0};
std::atomic<uint64_t> g_sink {0};

bool HasFlag(uint32_t mode_flags, uint32_t flag)
{
    return (mode_flags & flag) != 0;
}

void Usage()
{
    std::printf(
        "Usage: linux_pseudo_hook_benchmark "
        "[-o output] [-t threads] [-d duration_sec] [-s alloc_size] "
        "[-m baseline|timestamp|atomic|stack|buffer|combined|csv] "
        "[-k stack_depth] [-r record_size]\n");
}

bool ParsePositiveInt(const char* value, int* out)
{
    if (value == nullptr || out == nullptr) {
        return false;
    }
    char* end_ptr = nullptr;
    long parsed = std::strtol(value, &end_ptr, 10);
    if (*value == '\0' || end_ptr == nullptr || *end_ptr != '\0' || parsed <= 0 || parsed > INT32_MAX) {
        return false;
    }
    *out = static_cast<int>(parsed);
    return true;
}

uint32_t ParseModeFlags(const std::string& mode_text)
{
    if (mode_text == "baseline") {
        return MODE_BASELINE;
    }
    if (mode_text == "timestamp") {
        return MODE_TIMESTAMP;
    }
    if (mode_text == "atomic") {
        return MODE_ATOMIC;
    }
    if (mode_text == "stack") {
        return MODE_STACK;
    }
    if (mode_text == "buffer") {
        return MODE_BUFFER;
    }
    if (mode_text == "combined") {
        return MODE_TIMESTAMP | MODE_ATOMIC | MODE_STACK | MODE_BUFFER;
    }

    uint32_t flags = MODE_BASELINE;
    size_t begin = 0;
    while (begin < mode_text.size()) {
        size_t end = mode_text.find(',', begin);
        std::string token = mode_text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (token == "timestamp") {
            flags |= MODE_TIMESTAMP;
        } else if (token == "atomic") {
            flags |= MODE_ATOMIC;
        } else if (token == "stack") {
            flags |= MODE_STACK;
        } else if (token == "buffer") {
            flags |= MODE_BUFFER;
        } else if (!token.empty()) {
            return UINT32_MAX;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return flags;
}

bool ParseArgs(int argc, char* argv[], Config* config)
{
    if (config == nullptr) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                return false;
            }
            config->output_path = argv[++i];
        } else if (std::strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], &config->thread_count)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "-d") == 0) {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], &config->duration_seconds)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], &config->alloc_size)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "-k") == 0) {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], &config->stack_depth)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "-r") == 0) {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], &config->record_size)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "-m") == 0) {
            if (i + 1 >= argc) {
                return false;
            }
            config->mode_name = argv[++i];
            config->mode_flags = ParseModeFlags(config->mode_name);
            if (config->mode_flags == UINT32_MAX) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

void ApplyTimestampOverhead()
{
    timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_sink.fetch_add(static_cast<uint64_t>(ts.tv_nsec), std::memory_order_relaxed);
}

void ApplyAtomicOverhead()
{
    g_shared_counter.fetch_add(1, std::memory_order_relaxed);
    g_shared_counter.fetch_add(3, std::memory_order_relaxed);
}

void ApplyStackOverhead(int stack_depth)
{
    constexpr int kMaxFrames = 64;
    void* frames[kMaxFrames];
    int depth = std::min(stack_depth, kMaxFrames);
    int captured = backtrace(frames, depth);
    g_sink.fetch_add(static_cast<uint64_t>(captured), std::memory_order_relaxed);
}

void ApplyBufferOverhead(std::vector<unsigned char>* ring_buffer, size_t* cursor, int alloc_size, int record_size)
{
    if (ring_buffer == nullptr || cursor == nullptr || ring_buffer->empty()) {
        return;
    }

    const unsigned char fill = static_cast<unsigned char>(alloc_size & 0xFF);
    size_t pos = *cursor % ring_buffer->size();
    size_t first = std::min(static_cast<size_t>(record_size), ring_buffer->size() - pos);
    std::memset(ring_buffer->data() + pos, fill, first);
    if (static_cast<size_t>(record_size) > first) {
        std::memset(ring_buffer->data(), fill, static_cast<size_t>(record_size) - first);
    }
    *cursor += static_cast<size_t>(record_size);
}

void WorkerMain(const Config& config, ThreadResult* result)
{
    if (result == nullptr) {
        return;
    }

    std::vector<unsigned char> ring_buffer;
    size_t cursor = 0;
    if (HasFlag(config.mode_flags, MODE_BUFFER)) {
        size_t bytes = std::max(static_cast<size_t>(config.record_size) * 1024U, static_cast<size_t>(1U << 20));
        ring_buffer.resize(bytes, 0);
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (elapsed >= config.duration_seconds) {
            break;
        }

        void* mem = std::malloc(static_cast<size_t>(config.alloc_size));
        if (mem == nullptr) {
            break;
        }

        static_cast<unsigned char*>(mem)[0] = static_cast<unsigned char>(result->iterations & 0xFFU);

        if (HasFlag(config.mode_flags, MODE_TIMESTAMP)) {
            ApplyTimestampOverhead();
        }
        if (HasFlag(config.mode_flags, MODE_ATOMIC)) {
            ApplyAtomicOverhead();
        }
        if (HasFlag(config.mode_flags, MODE_STACK)) {
            ApplyStackOverhead(config.stack_depth);
        }
        if (HasFlag(config.mode_flags, MODE_BUFFER)) {
            ApplyBufferOverhead(&ring_buffer, &cursor, config.alloc_size, config.record_size);
        }

        std::free(mem);
        ++result->iterations;
    }

    g_sink.fetch_add(static_cast<uint64_t>(cursor), std::memory_order_relaxed);
}

void WriteOutput(const Config& config, uint64_t total_iterations)
{
    if (config.output_path.empty()) {
        return;
    }

    FILE* fp = std::fopen(config.output_path.c_str(), "w");
    if (fp == nullptr) {
        std::perror("fopen");
        return;
    }

    double throughput = static_cast<double>(total_iterations) / static_cast<double>(config.duration_seconds);
    std::fprintf(fp, "Mode: %s\n", config.mode_name.c_str());
    std::fprintf(fp, "Threads: %d\n", config.thread_count);
    std::fprintf(fp, "Duration: %d\n", config.duration_seconds);
    std::fprintf(fp, "Alloc size: %d\n", config.alloc_size);
    std::fprintf(fp, "Stack depth: %d\n", config.stack_depth);
    std::fprintf(fp, "Record size: %d\n", config.record_size);
    std::fprintf(fp, "Total iterations: %llu\n", static_cast<unsigned long long>(total_iterations));
    std::fprintf(fp, "Throughput ops/s: %.2f\n", throughput);
    std::fclose(fp);
}

} // namespace

int main(int argc, char* argv[])
{
    Config config;
    if (!ParseArgs(argc, argv, &config)) {
        Usage();
        return 1;
    }

    std::vector<ThreadResult> results(static_cast<size_t>(config.thread_count));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(config.thread_count));

    for (int i = 0; i < config.thread_count; ++i) {
        workers.emplace_back(WorkerMain, std::cref(config), &results[static_cast<size_t>(i)]);
    }
    for (auto& worker : workers) {
        worker.join();
    }

    uint64_t total_iterations = 0;
    for (const auto& result : results) {
        total_iterations += result.iterations;
    }

    double throughput = static_cast<double>(total_iterations) / static_cast<double>(config.duration_seconds);
    std::printf("Mode: %s\n", config.mode_name.c_str());
    std::printf("Threads: %d\n", config.thread_count);
    std::printf("Duration: %d sec\n", config.duration_seconds);
    std::printf("Alloc size: %d bytes\n", config.alloc_size);
    std::printf("Total iterations: %llu\n", static_cast<unsigned long long>(total_iterations));
    std::printf("Throughput ops/s: %.2f\n", throughput);

    WriteOutput(config, total_iterations);
    return 0;
}
