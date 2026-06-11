#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

namespace {

enum class AllocPattern { kMallocOnly, kMixed3 };

struct Config {
    int thread_count = 1;
    int duration_seconds = 10;
    int alloc_size = 32;
    AllocPattern pattern = AllocPattern::kMallocOnly;
    uint64_t iterations_per_thread = 0;
    int skew_pct = 0;
};

std::atomic<uint64_t> g_total_iterations{0};

bool ParsePositiveInt(const char* text, int* out_value) {
    if (text == nullptr || out_value == nullptr) return false;
    char* end = nullptr;
    long v = std::strtol(text, &end, 10);
    if (*text == '\0' || end == nullptr || *end != '\0' || v <= 0 || v > INT32_MAX) return false;
    *out_value = static_cast<int>(v);
    return true;
}

bool ParsePositiveUint64(const char* text, uint64_t* out_value) {
    if (text == nullptr || out_value == nullptr) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(text, &end, 10);
    if (*text == '\0' || end == nullptr || *end != '\0' || v == 0) return false;
    *out_value = v;
    return true;
}

void PrintUsage() {
    std::printf("Usage: perf_test_data_linux [--threads n] [--duration sec] [--size bytes] "
                "[--iters-per-thread n] [--pattern malloc_only|mixed3] [--skew P]\n");
}

const char* PatternName(AllocPattern p) {
    switch (p) {
        case AllocPattern::kMallocOnly: return "malloc_only";
        case AllocPattern::kMixed3: return "mixed3";
    }
    return "unknown";
}

bool ParsePattern(const char* text, AllocPattern* out) {
    if (std::strcmp(text, "malloc_only") == 0) { *out = AllocPattern::kMallocOnly; return true; }
    if (std::strcmp(text, "mixed3") == 0) { *out = AllocPattern::kMixed3; return true; }
    return false;
}

bool ParseArgs(int argc, char* argv[], Config* config) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], &config->thread_count)) { PrintUsage(); return false; }
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], &config->duration_seconds)) { PrintUsage(); return false; }
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], &config->alloc_size)) { PrintUsage(); return false; }
        } else if (std::strcmp(argv[i], "--iters-per-thread") == 0 && i + 1 < argc) {
            if (!ParsePositiveUint64(argv[++i], &config->iterations_per_thread)) { PrintUsage(); return false; }
        } else if (std::strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) {
            if (!ParsePattern(argv[++i], &config->pattern)) { PrintUsage(); return false; }
        } else if (std::strcmp(argv[i], "--skew") == 0 && i + 1 < argc) {
            char* end = nullptr;
            long v = std::strtol(argv[++i], &end, 10);
            if (end == nullptr || *end != '\0' || v < 0 || v > 99) { PrintUsage(); return false; }
            config->skew_pct = static_cast<int>(v);
        } else {
            PrintUsage(); return false;
        }
    }
    return true;
}

bool RunMallocOnlyIteration(size_t alloc_size) {
    void* mem = std::malloc(alloc_size);
    if (mem == nullptr) return false;
    static_cast<unsigned char*>(mem)[0] = 0xAB;
    std::free(mem);
    return true;
}

bool RunMixed3Iteration(size_t alloc_size, uint64_t iteration) {
    switch (iteration % 3) {
    case 0: return RunMallocOnlyIteration(alloc_size);
    case 1: {
        void* mem = std::calloc(1, alloc_size);
        if (mem == nullptr) return false;
        static_cast<unsigned char*>(mem)[0] = 0xCD;
        std::free(mem);
        return true;
    }
    case 2: {
        void* mem = std::malloc(alloc_size);
        if (mem == nullptr) return false;
        static_cast<unsigned char*>(mem)[0] = 0xEF;
        void* resized = std::realloc(mem, alloc_size * 2);
        if (resized == nullptr) { std::free(mem); return false; }
        static_cast<unsigned char*>(resized)[0] = 0x12;
        std::free(resized);
        return true;
    }
    }
    return false;
}

void WorkerMain(const Config& config, int thread_idx) {
    uint64_t my_iters = config.iterations_per_thread;
    if (config.iterations_per_thread > 0) {
        if (config.skew_pct > 0 && config.thread_count > 1) {
            uint64_t total = static_cast<uint64_t>(config.iterations_per_thread)
                * static_cast<uint64_t>(config.thread_count);
            if (thread_idx == 0) {
                my_iters = total * static_cast<uint64_t>(config.skew_pct) / 100ULL;
            } else {
                uint64_t remaining = total * static_cast<uint64_t>(100 - config.skew_pct) / 100ULL;
                my_iters = remaining / static_cast<uint64_t>(config.thread_count - 1);
            }
        }
        std::function<bool(size_t)> iterFn;
        if (config.pattern == AllocPattern::kMixed3) {
            for (uint64_t i = 0; i < my_iters; ++i) {
                if (!RunMixed3Iteration(static_cast<size_t>(config.alloc_size), i)) break;
                g_total_iterations.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            for (uint64_t i = 0; i < my_iters; ++i) {
                if (!RunMallocOnlyIteration(static_cast<size_t>(config.alloc_size))) break;
                g_total_iterations.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return;
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() >= config.duration_seconds) break;
        if (config.pattern == AllocPattern::kMixed3) {
            RunMixed3Iteration(static_cast<size_t>(config.alloc_size), g_total_iterations.load());
        } else {
            RunMallocOnlyIteration(static_cast<size_t>(config.alloc_size));
        }
        g_total_iterations.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    Config config;
    if (!ParseArgs(argc, argv, &config)) return 1;

    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(config.thread_count));
    for (int i = 0; i < config.thread_count; ++i)
        workers.emplace_back(WorkerMain, std::cref(config), i);
    for (auto& w : workers) w.join();
    auto end = std::chrono::steady_clock::now();

    uint64_t total = g_total_iterations.load();
    double elapsed = std::chrono::duration<double>(end - start).count();
    uint64_t target = config.iterations_per_thread > 0
        ? config.iterations_per_thread * static_cast<uint64_t>(config.thread_count) : 0;
    double throughput = elapsed > 0.0 ? static_cast<double>(total) / elapsed : 0.0;

    std::printf("mode=%s threads=%d duration=%d alloc_size=%d pattern=%s skew=%d "
                "iterations_per_thread=%llu total_target_iterations=%llu total_iterations=%llu "
                "elapsed_seconds=%.9f throughput_ops=%.2f\n",
        config.iterations_per_thread > 0 ? "fixed" : "duration",
        config.thread_count, config.duration_seconds, config.alloc_size,
        PatternName(config.pattern), config.skew_pct,
        static_cast<unsigned long long>(config.iterations_per_thread),
        static_cast<unsigned long long>(target),
        static_cast<unsigned long long>(total),
        elapsed, throughput);
    return 0;
}
