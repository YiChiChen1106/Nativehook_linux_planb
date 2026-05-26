#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

enum class AllocPattern {
    kMallocOnly,
    kMixed3,
};

struct Config {
    int thread_count = 1;
    int duration_seconds = 10;
    int alloc_size = 32;
    AllocPattern pattern = AllocPattern::kMallocOnly;
};

std::atomic<uint64_t> g_total_iterations {0};

bool ParsePositiveInt(const char* text, int* out_value)
{
    if (text == nullptr || out_value == nullptr) {
        return false;
    }
    char* end_ptr = nullptr;
    long value = std::strtol(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || value <= 0 || value > INT32_MAX) {
        return false;
    }
    *out_value = static_cast<int>(value);
    return true;
}

void PrintUsage()
{
    std::printf("Usage: perf_test_data_linux [--threads n] [--duration sec] [--size bytes] [--pattern malloc_only|mixed3]\n");
}

const char* PatternName(AllocPattern pattern)
{
    switch (pattern) {
        case AllocPattern::kMallocOnly:
            return "malloc_only";
        case AllocPattern::kMixed3:
            return "mixed3";
    }
    return "unknown";
}

bool ParsePattern(const char* text, AllocPattern* out_pattern)
{
    if (text == nullptr || out_pattern == nullptr) {
        return false;
    }
    if (std::strcmp(text, "malloc_only") == 0) {
        *out_pattern = AllocPattern::kMallocOnly;
        return true;
    }
    if (std::strcmp(text, "mixed3") == 0) {
        *out_pattern = AllocPattern::kMixed3;
        return true;
    }
    return false;
}

bool RunMallocOnlyIteration(size_t alloc_size)
{
    void* mem = std::malloc(alloc_size);
    if (mem == nullptr) {
        return false;
    }
    static_cast<unsigned char*>(mem)[0] = 0xAB;
    std::free(mem);
    return true;
}

bool RunMixed3Iteration(size_t alloc_size, uint64_t iteration)
{
    const uint64_t selector = iteration % 3;
    if (selector == 0) {
        return RunMallocOnlyIteration(alloc_size);
    }

    if (selector == 1) {
        void* mem = std::calloc(1, alloc_size);
        if (mem == nullptr) {
            return false;
        }
        static_cast<unsigned char*>(mem)[0] = 0xCD;
        std::free(mem);
        return true;
    }

    void* mem = std::malloc(alloc_size);
    if (mem == nullptr) {
        return false;
    }
    static_cast<unsigned char*>(mem)[0] = 0xEF;

    const size_t realloc_size = alloc_size + alloc_size;
    void* resized = std::realloc(mem, realloc_size);
    if (resized == nullptr) {
        std::free(mem);
        return false;
    }
    static_cast<unsigned char*>(resized)[0] = 0x12;
    std::free(resized);
    return true;
}

bool ParseArgs(int argc, char* argv[], Config* config)
{
    if (config == nullptr) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], &config->thread_count)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], &config->duration_seconds)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], &config->alloc_size)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) {
            if (!ParsePattern(argv[++i], &config->pattern)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

void WorkerMain(const Config& config)
{
    auto start = std::chrono::steady_clock::now();
    uint64_t local_iterations = 0;
    const size_t alloc_size = static_cast<size_t>(config.alloc_size);
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (elapsed >= config.duration_seconds) {
            break;
        }

        const bool ok = config.pattern == AllocPattern::kMallocOnly
            ? RunMallocOnlyIteration(alloc_size)
            : RunMixed3Iteration(alloc_size, local_iterations);
        if (!ok) {
            break;
        }
        ++local_iterations;
        g_total_iterations.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    Config config;
    if (!ParseArgs(argc, argv, &config)) {
        PrintUsage();
        return 1;
    }

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(config.thread_count));
    for (int i = 0; i < config.thread_count; ++i) {
        workers.emplace_back(WorkerMain, std::cref(config));
    }
    for (auto& worker : workers) {
        worker.join();
    }

    const uint64_t total_iterations = g_total_iterations.load(std::memory_order_relaxed);
    const double throughput = static_cast<double>(total_iterations) / static_cast<double>(config.duration_seconds);
    std::printf("threads=%d duration=%d alloc_size=%d pattern=%s total_iterations=%llu throughput_ops=%.2f\n",
        config.thread_count,
        config.duration_seconds,
        config.alloc_size,
        PatternName(config.pattern),
        static_cast<unsigned long long>(total_iterations),
        throughput);
    return 0;
}
