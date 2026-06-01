#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {

struct TestConfig {
    int cross_alloc_count = 400;
    int same_thread_count = 200;
    int multi_thread_count = 100;
    int alloc_size = 32;
};

TestConfig ParseArgs(int argc, char** argv)
{
    TestConfig cfg;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cross") == 0 && i + 1 < argc) {
            cfg.cross_alloc_count = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--same") == 0 && i + 1 < argc) {
            cfg.same_thread_count = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--multi") == 0 && i + 1 < argc) {
            cfg.multi_thread_count = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            cfg.alloc_size = std::atoi(argv[++i]);
        }
    }
    return cfg;
}

}  // namespace

int main(int argc, char** argv)
{
    const TestConfig cfg = ParseArgs(argc, argv);

    std::vector<void*> shared_ptrs(cfg.cross_alloc_count, nullptr);

    std::thread alloc_thread([&]() {
        for (int i = 0; i < cfg.cross_alloc_count; ++i) {
            shared_ptrs[i] = std::malloc(cfg.alloc_size);
        }
    });
    alloc_thread.join();

    std::thread cross_free_thread([&]() {
        for (int i = 0; i < cfg.cross_alloc_count; ++i) {
            if (shared_ptrs[i] != nullptr) {
                std::free(shared_ptrs[i]);
                shared_ptrs[i] = nullptr;
            }
        }
    });
    cross_free_thread.join();

    std::thread same_thread([&]() {
        for (int i = 0; i < cfg.same_thread_count; ++i) {
            void* p = std::malloc(cfg.alloc_size);
            std::free(p);
        }
    });
    same_thread.join();

    std::thread t4([&]() {
        std::vector<void*> local(cfg.multi_thread_count, nullptr);
        for (int i = 0; i < cfg.multi_thread_count; ++i) {
            local[i] = std::malloc(cfg.alloc_size);
        }
        for (int i = 0; i < cfg.multi_thread_count; ++i) {
            std::free(local[i]);
        }
    });
    std::thread t5([&]() {
        std::vector<void*> local(cfg.multi_thread_count, nullptr);
        for (int i = 0; i < cfg.multi_thread_count; ++i) {
            local[i] = std::malloc(cfg.alloc_size);
        }
        for (int i = 0; i < cfg.multi_thread_count; ++i) {
            std::free(local[i]);
        }
    });
    t4.join();
    t5.join();

    std::cout << "done cross=" << cfg.cross_alloc_count
              << " same=" << cfg.same_thread_count
              << " multi=" << cfg.multi_thread_count
              << " size=" << cfg.alloc_size << "\n";
    return 0;
}
