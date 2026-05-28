#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

struct Config {
    int thread_count = 1;
    int duration_seconds = 10;
    int alloc_size = 32;
    uint64_t iterations_per_thread = 0;
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

bool ParsePositiveUint64(const char* text, uint64_t* out_value)
{
    if (text == nullptr || out_value == nullptr) {
        return false;
    }
    char* end_ptr = nullptr;
    errno = 0;
    unsigned long long value = std::strtoull(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || errno != 0 || value == 0) {
        return false;
    }
    *out_value = static_cast<uint64_t>(value);
    return true;
}

void PrintUsage()
{
    std::printf("Usage: perf_test_data_linux [--threads n] [--duration sec] [--size bytes] [--iters-per-thread n]\n");
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
        } else if (std::strcmp(argv[i], "--iters-per-thread") == 0 && i + 1 < argc) {
            if (!ParsePositiveUint64(argv[++i], &config->iterations_per_thread)) {
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
    if (config.iterations_per_thread > 0) {
        for (uint64_t i = 0; i < config.iterations_per_thread; ++i) {
            void* mem = std::malloc(static_cast<size_t>(config.alloc_size));
            if (mem == nullptr) {
                break;
            }
            static_cast<unsigned char*>(mem)[0] = 0xAB;
            std::free(mem);
            g_total_iterations.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

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

    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(config.thread_count));
    for (int i = 0; i < config.thread_count; ++i) {
        workers.emplace_back(WorkerMain, std::cref(config));
    }
    for (auto& worker : workers) {
        worker.join();
    }
    const auto end = std::chrono::steady_clock::now();

    const uint64_t total_iterations = g_total_iterations.load(std::memory_order_relaxed);
    const double elapsed_seconds = std::chrono::duration<double>(end - start).count();
    const double denominator = config.iterations_per_thread > 0 ? elapsed_seconds : static_cast<double>(config.duration_seconds);
    const double throughput = denominator > 0.0 ? static_cast<double>(total_iterations) / denominator : 0.0;
    const uint64_t total_target_iterations = config.iterations_per_thread * static_cast<uint64_t>(config.thread_count);
    std::printf("mode=%s threads=%d duration=%d alloc_size=%d iterations_per_thread=%llu total_target_iterations=%llu total_iterations=%llu elapsed_seconds=%.9f throughput_ops=%.2f\n",
        config.iterations_per_thread > 0 ? "fixed" : "duration",
        config.thread_count,
        config.duration_seconds,
        config.alloc_size,
        static_cast<unsigned long long>(config.iterations_per_thread),
        static_cast<unsigned long long>(total_target_iterations),
        static_cast<unsigned long long>(total_iterations),
        elapsed_seconds,
        throughput);
    return 0;
}
