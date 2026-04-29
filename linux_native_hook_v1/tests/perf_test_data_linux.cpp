#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

struct Config {
    int thread_count = 1;
    int duration_seconds = 10;
    int alloc_size = 32;
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
    std::printf("Usage: perf_test_data_linux [--threads n] [--duration sec] [--size bytes]\n");
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
        } else {
            return false;
        }
    }
    return true;
}

void WorkerMain(const Config& config)
{
    auto start = std::chrono::steady_clock::now();
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (elapsed >= config.duration_seconds) {
            break;
        }

        void* mem = std::malloc(static_cast<size_t>(config.alloc_size));
        if (mem == nullptr) {
            break;
        }
        static_cast<unsigned char*>(mem)[0] = 0xAB;
        std::free(mem);
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
    std::printf("threads=%d duration=%d alloc_size=%d total_iterations=%llu throughput_ops=%.2f\n",
        config.thread_count,
        config.duration_seconds,
        config.alloc_size,
        static_cast<unsigned long long>(total_iterations),
        throughput);
    return 0;
}
