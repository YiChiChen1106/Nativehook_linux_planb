#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

constexpr size_t kMaxCount = 200000;
void* g_ptrs[kMaxCount] {};
std::atomic<size_t> g_allocated {0};
std::atomic<size_t> g_freed {0};

size_t ParseSizeArg(const char* text, size_t fallback)
{
    if (text == nullptr || text[0] == '\0') {
        return fallback;
    }
    char* end_ptr = nullptr;
    const unsigned long value = std::strtoul(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || value == 0) {
        return fallback;
    }
    return static_cast<size_t>(value);
}

}  // namespace

int main(int argc, char** argv)
{
    size_t count = 10000;
    size_t size = 32;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            count = ParseSizeArg(argv[++i], count);
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            size = ParseSizeArg(argv[++i], size);
        } else {
            std::cerr << "usage: cross_thread_free_linux [--count n] [--size bytes]\n";
            return 2;
        }
    }

    if (count > kMaxCount) {
        std::cerr << "count exceeds max_count=" << kMaxCount << "\n";
        return 2;
    }

    std::thread alloc_thread([&]() {
        for (size_t i = 0; i < count; ++i) {
            g_ptrs[i] = std::malloc(size);
            if (g_ptrs[i] != nullptr) {
                g_allocated.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    alloc_thread.join();

    std::thread free_thread([&]() {
        for (size_t i = 0; i < count; ++i) {
            if (g_ptrs[i] != nullptr) {
                std::free(g_ptrs[i]);
                g_ptrs[i] = nullptr;
                g_freed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    free_thread.join();

    std::cout << "count=" << count
              << " size=" << size
              << " allocated=" << g_allocated.load(std::memory_order_relaxed)
              << " freed=" << g_freed.load(std::memory_order_relaxed)
              << "\n";
    return g_allocated.load(std::memory_order_relaxed) == g_freed.load(std::memory_order_relaxed) ? 0 : 1;
}
