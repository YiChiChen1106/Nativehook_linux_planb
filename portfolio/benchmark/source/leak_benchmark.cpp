#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <vector>

namespace {

enum class BenchmarkCase {
    kNoLeak,
    kDefiniteLeak,
    kGrowthLeak,
    kDelayedFree,
    kHighFreqNoLeak,
    kMixed,
    kGameSceneNoLeak,
    kGameSceneLeak,
    kGameSceneGrowth,
    kGameAsyncDelayedFree,
    kGameFrameNoLeak,
    kGameMixed,
    kGameNetworkPacket,
    kGameAudioDecode,
    kGameCacheEviction,
    kGameObjectPoolNoLeak,
    kGameObjectPoolLeak,
    kGameMmapNoLeak,
    kGameMmapLeak,
    kGameUiCallbackNoLeak,
    kGameUiCallbackLeak,
    kGameLongRunningNoLeak,
    kGameLongRunningLeak,
};

struct Config {
    BenchmarkCase benchmark_case = BenchmarkCase::kNoLeak;
    bool has_case = false;
    int threads = 1;
    uint64_t iterations = 10000;
    size_t size = 64;
    int rounds = 8;
    int start_delay_ms = 0;
    int delay_ms = 200;
    int post_delay_ms = 0;
    bool json = false;
    bool has_expect_allocations = false;
    bool has_expect_frees = false;
    bool has_expect_outstanding_blocks = false;
    bool has_expect_outstanding_bytes = false;
    uint64_t expect_allocations = 0;
    uint64_t expect_frees = 0;
    uint64_t expect_outstanding_blocks = 0;
    uint64_t expect_outstanding_bytes = 0;
};

struct TruthCheckpoint {
    std::string name;
    uint64_t outstanding_blocks = 0;
    uint64_t outstanding_bytes = 0;
};

struct Stats {
    uint64_t allocations = 0;
    uint64_t frees = 0;
    uint64_t expected_outstanding_blocks = 0;
    uint64_t expected_outstanding_bytes = 0;
    std::vector<TruthCheckpoint> truth_checkpoints;
};

struct CaseInfo {
    const char* name;
    const char* purpose;
    bool intentionally_leaky;
    const char* suite_group;
};

std::atomic<uintptr_t> g_sink {0};

CaseInfo GetCaseInfo(BenchmarkCase benchmark_case)
{
    switch (benchmark_case) {
        case BenchmarkCase::kNoLeak:
            return {"no_leak", "balanced allocations; used to observe false positives", false, "baseline"};
        case BenchmarkCase::kDefiniteLeak:
            return {"definite_leak", "known leaked blocks from one clear call path", true, "baseline"};
        case BenchmarkCase::kGrowthLeak:
            return {"growth_leak", "outstanding heap grows round by round", true, "baseline"};
        case BenchmarkCase::kDelayedFree:
            return {"delayed_free", "long-lived objects are eventually released", false, "baseline"};
        case BenchmarkCase::kHighFreqNoLeak:
            return {"high_freq_no_leak", "high-frequency balanced path for overhead measurement", false, "baseline"};
        case BenchmarkCase::kMixed:
            return {"mixed", "balanced churn, delayed frees, and known leaks in one workload", true, "baseline"};
        case BenchmarkCase::kGameSceneNoLeak:
            return {"game_scene_no_leak", "scene asset load and unload with balanced lifetime", false, "scene"};
        case BenchmarkCase::kGameSceneLeak:
            return {"game_scene_leak", "scene transition leaves one asset path unreleased", true, "scene"};
        case BenchmarkCase::kGameSceneGrowth:
            return {"game_scene_growth", "repeated scene transitions accumulate asset objects", true, "scene"};
        case BenchmarkCase::kGameAsyncDelayedFree:
            return {"game_async_delayed_free", "async resource buffers are freed by a reclaimer thread", false, "async"};
        case BenchmarkCase::kGameFrameNoLeak:
            return {"game_frame_no_leak", "per-frame temporary objects are created and released", false, "frame"};
        case BenchmarkCase::kGameMixed:
            return {"game_mixed", "frame churn, UI objects, async buffers, and known leaks are mixed", true, "async"};
        case BenchmarkCase::kGameNetworkPacket:
            return {"game_network_packet", "packet processing leaves malformed packet buffers unreleased", true, "business"};
        case BenchmarkCase::kGameAudioDecode:
            return {"game_audio_decode", "audio decode failures leave grown PCM buffers unreleased", true, "business"};
        case BenchmarkCase::kGameCacheEviction:
            return {"game_cache_eviction", "cache eviction leaves one retained resource unreleased", true, "business"};
        case BenchmarkCase::kGameObjectPoolNoLeak:
            return {"game_object_pool_no_leak", "object pool returns every borrowed object", false, "business_edges"};
        case BenchmarkCase::kGameObjectPoolLeak:
            return {"game_object_pool_leak", "object pool loses selected borrowed objects", true, "business_edges"};
        case BenchmarkCase::kGameMmapNoLeak:
            return {"game_mmap_no_leak", "resource mappings are released with munmap", false, "business_edges"};
        case BenchmarkCase::kGameMmapLeak:
            return {"game_mmap_leak", "resource mappings remain mapped after use", true, "business_edges"};
        case BenchmarkCase::kGameUiCallbackNoLeak:
            return {"game_ui_callback_no_leak", "UI callback owner and listener are cancelled", false, "business_edges"};
        case BenchmarkCase::kGameUiCallbackLeak:
            return {"game_ui_callback_leak", "UI callback owner and listener retain each other", true, "business_edges"};
        case BenchmarkCase::kGameLongRunningNoLeak:
            return {"game_long_running_no_leak", "long-running cycles release every temporary object", false, "business_edges"};
        case BenchmarkCase::kGameLongRunningLeak:
            return {"game_long_running_leak", "long-running cycles accumulate one leak path", true, "business_edges"};
    }
    return {"unknown", "unknown", false, "unknown"};
}

bool ParseCase(const char* text, BenchmarkCase* benchmark_case)
{
    if (text == nullptr || benchmark_case == nullptr) {
        return false;
    }
    if (std::strcmp(text, "no_leak") == 0) {
        *benchmark_case = BenchmarkCase::kNoLeak;
        return true;
    }
    if (std::strcmp(text, "definite_leak") == 0) {
        *benchmark_case = BenchmarkCase::kDefiniteLeak;
        return true;
    }
    if (std::strcmp(text, "growth_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGrowthLeak;
        return true;
    }
    if (std::strcmp(text, "delayed_free") == 0) {
        *benchmark_case = BenchmarkCase::kDelayedFree;
        return true;
    }
    if (std::strcmp(text, "high_freq_no_leak") == 0) {
        *benchmark_case = BenchmarkCase::kHighFreqNoLeak;
        return true;
    }
    if (std::strcmp(text, "mixed") == 0) {
        *benchmark_case = BenchmarkCase::kMixed;
        return true;
    }
    if (std::strcmp(text, "game_scene_no_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameSceneNoLeak;
        return true;
    }
    if (std::strcmp(text, "game_scene_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameSceneLeak;
        return true;
    }
    if (std::strcmp(text, "game_scene_growth") == 0) {
        *benchmark_case = BenchmarkCase::kGameSceneGrowth;
        return true;
    }
    if (std::strcmp(text, "game_async_delayed_free") == 0) {
        *benchmark_case = BenchmarkCase::kGameAsyncDelayedFree;
        return true;
    }
    if (std::strcmp(text, "game_frame_no_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameFrameNoLeak;
        return true;
    }
    if (std::strcmp(text, "game_mixed") == 0) {
        *benchmark_case = BenchmarkCase::kGameMixed;
        return true;
    }
    if (std::strcmp(text, "game_network_packet") == 0) {
        *benchmark_case = BenchmarkCase::kGameNetworkPacket;
        return true;
    }
    if (std::strcmp(text, "game_audio_decode") == 0) {
        *benchmark_case = BenchmarkCase::kGameAudioDecode;
        return true;
    }
    if (std::strcmp(text, "game_cache_eviction") == 0) {
        *benchmark_case = BenchmarkCase::kGameCacheEviction;
        return true;
    }
    if (std::strcmp(text, "game_object_pool_no_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameObjectPoolNoLeak;
        return true;
    }
    if (std::strcmp(text, "game_object_pool_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameObjectPoolLeak;
        return true;
    }
    if (std::strcmp(text, "game_mmap_no_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameMmapNoLeak;
        return true;
    }
    if (std::strcmp(text, "game_mmap_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameMmapLeak;
        return true;
    }
    if (std::strcmp(text, "game_ui_callback_no_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameUiCallbackNoLeak;
        return true;
    }
    if (std::strcmp(text, "game_ui_callback_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameUiCallbackLeak;
        return true;
    }
    if (std::strcmp(text, "game_long_running_no_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameLongRunningNoLeak;
        return true;
    }
    if (std::strcmp(text, "game_long_running_leak") == 0) {
        *benchmark_case = BenchmarkCase::kGameLongRunningLeak;
        return true;
    }
    return false;
}

bool ParsePositiveInt(const char* text, int* value)
{
    if (text == nullptr || value == nullptr || text[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (end == nullptr || *end != '\0' || parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

bool ParseNonNegativeInt(const char* text, int* value)
{
    if (text == nullptr || value == nullptr || text[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (end == nullptr || *end != '\0' || parsed < 0 || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

bool ParsePositiveUint64(const char* text, uint64_t* value)
{
    if (text == nullptr || value == nullptr || text[0] == '\0') {
        return false;
    }
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    char* end = nullptr;
    errno = 0;
    unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno == ERANGE || end == nullptr || *end != '\0' || parsed == 0) {
        return false;
    }
    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseNonNegativeUint64(const char* text, uint64_t* value)
{
    if (text == nullptr || value == nullptr || text[0] == '\0') {
        return false;
    }
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    char* end = nullptr;
    errno = 0;
    unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno == ERANGE || end == nullptr || *end != '\0') {
        return false;
    }
    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool ParsePositiveSize(const char* text, size_t* value)
{
    uint64_t parsed = 0;
    if (!ParsePositiveUint64(text, &parsed) || parsed > std::numeric_limits<size_t>::max()) {
        return false;
    }
    *value = static_cast<size_t>(parsed);
    return true;
}

void PrintCases()
{
    const BenchmarkCase cases[] = {
        BenchmarkCase::kNoLeak,
        BenchmarkCase::kDefiniteLeak,
        BenchmarkCase::kGrowthLeak,
        BenchmarkCase::kDelayedFree,
        BenchmarkCase::kHighFreqNoLeak,
        BenchmarkCase::kMixed,
        BenchmarkCase::kGameSceneNoLeak,
        BenchmarkCase::kGameSceneLeak,
        BenchmarkCase::kGameSceneGrowth,
        BenchmarkCase::kGameAsyncDelayedFree,
        BenchmarkCase::kGameFrameNoLeak,
        BenchmarkCase::kGameMixed,
        BenchmarkCase::kGameNetworkPacket,
        BenchmarkCase::kGameAudioDecode,
        BenchmarkCase::kGameCacheEviction,
        BenchmarkCase::kGameObjectPoolNoLeak,
        BenchmarkCase::kGameObjectPoolLeak,
        BenchmarkCase::kGameMmapNoLeak,
        BenchmarkCase::kGameMmapLeak,
        BenchmarkCase::kGameUiCallbackNoLeak,
        BenchmarkCase::kGameUiCallbackLeak,
        BenchmarkCase::kGameLongRunningNoLeak,
        BenchmarkCase::kGameLongRunningLeak,
    };
    for (BenchmarkCase benchmark_case : cases) {
        const CaseInfo info = GetCaseInfo(benchmark_case);
        std::cout << info.name << " - " << info.purpose << "\n";
    }
}

void PrintUsage()
{
    std::cout
        << "Usage: leak_benchmark --case <name> [options]\n"
        << "\n"
        << "Cases:\n"
        << "  no_leak definite_leak growth_leak delayed_free high_freq_no_leak mixed\n"
        << "  game_scene_no_leak game_scene_leak game_scene_growth\n"
        << "  game_async_delayed_free game_frame_no_leak game_mixed\n"
        << "  game_network_packet game_audio_decode game_cache_eviction\n"
        << "  game_object_pool_no_leak game_object_pool_leak\n"
        << "  game_mmap_no_leak game_mmap_leak\n"
        << "  game_ui_callback_no_leak game_ui_callback_leak\n"
        << "  game_long_running_no_leak game_long_running_leak\n"
        << "\n"
        << "Options:\n"
        << "  --threads N          worker threads, default 1\n"
        << "  --iterations N       iterations per thread, default 10000\n"
        << "  --size BYTES         base allocation size, default 64\n"
        << "  --rounds N           growth_leak rounds, default 8\n"
        << "  --start-delay-ms N   wait before case starts, default 0\n"
        << "  --delay-ms N         hold/sleep delay in ms, default 200\n"
        << "  --post-delay-ms N    wait after case completes, default 0\n"
        << "  --expect-allocations N\n"
        << "  --expect-frees N\n"
        << "  --expect-outstanding-blocks N\n"
        << "  --expect-outstanding-bytes N\n"
        << "  --json               emit JSON summary\n"
        << "  --list-cases         print case descriptions\n"
        << "  --help               print this help\n";
}

bool ParseArgs(int argc, char* argv[], Config* config)
{
    if (config == nullptr) {
        return false;
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc) {
            if (!ParseCase(argv[++i], &config->benchmark_case)) {
                std::cerr << "invalid case: " << argv[i] << "\n";
                return false;
            }
            config->has_case = true;
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], &config->threads)) {
                std::cerr << "invalid --threads\n";
                return false;
            }
        } else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            if (!ParsePositiveUint64(argv[++i], &config->iterations)) {
                std::cerr << "invalid --iterations\n";
                return false;
            }
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (!ParsePositiveSize(argv[++i], &config->size)) {
                std::cerr << "invalid --size\n";
                return false;
            }
        } else if (std::strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
            if (!ParsePositiveInt(argv[++i], &config->rounds)) {
                std::cerr << "invalid --rounds\n";
                return false;
            }
        } else if (std::strcmp(argv[i], "--start-delay-ms") == 0 && i + 1 < argc) {
            if (!ParseNonNegativeInt(argv[++i], &config->start_delay_ms)) {
                std::cerr << "invalid --start-delay-ms\n";
                return false;
            }
        } else if (std::strcmp(argv[i], "--delay-ms") == 0 && i + 1 < argc) {
            if (!ParseNonNegativeInt(argv[++i], &config->delay_ms)) {
                std::cerr << "invalid --delay-ms\n";
                return false;
            }
        } else if (std::strcmp(argv[i], "--post-delay-ms") == 0 && i + 1 < argc) {
            if (!ParseNonNegativeInt(argv[++i], &config->post_delay_ms)) {
                std::cerr << "invalid --post-delay-ms\n";
                return false;
            }
        } else if (std::strcmp(argv[i], "--expect-allocations") == 0 && i + 1 < argc) {
            if (!ParseNonNegativeUint64(argv[++i], &config->expect_allocations)) {
                std::cerr << "invalid --expect-allocations\n";
                return false;
            }
            config->has_expect_allocations = true;
        } else if (std::strcmp(argv[i], "--expect-frees") == 0 && i + 1 < argc) {
            if (!ParseNonNegativeUint64(argv[++i], &config->expect_frees)) {
                std::cerr << "invalid --expect-frees\n";
                return false;
            }
            config->has_expect_frees = true;
        } else if (std::strcmp(argv[i], "--expect-outstanding-blocks") == 0 && i + 1 < argc) {
            if (!ParseNonNegativeUint64(argv[++i], &config->expect_outstanding_blocks)) {
                std::cerr << "invalid --expect-outstanding-blocks\n";
                return false;
            }
            config->has_expect_outstanding_blocks = true;
        } else if (std::strcmp(argv[i], "--expect-outstanding-bytes") == 0 && i + 1 < argc) {
            if (!ParseNonNegativeUint64(argv[++i], &config->expect_outstanding_bytes)) {
                std::cerr << "invalid --expect-outstanding-bytes\n";
                return false;
            }
            config->has_expect_outstanding_bytes = true;
        } else if (std::strcmp(argv[i], "--json") == 0) {
            config->json = true;
        } else if (std::strcmp(argv[i], "--list-cases") == 0) {
            PrintCases();
            std::exit(0);
        } else if (std::strcmp(argv[i], "--help") == 0) {
            PrintUsage();
            std::exit(0);
        } else {
            std::cerr << "unknown or incomplete argument: " << argv[i] << "\n";
            return false;
        }
    }
    if (!config->has_case) {
        std::cerr << "missing required --case\n";
        return false;
    }
    return true;
}

void TouchMemory(void* ptr, size_t size, unsigned char value)
{
    if (ptr == nullptr || size == 0) {
        return;
    }
    auto* bytes = static_cast<volatile unsigned char*>(ptr);
    bytes[0] = value;
    bytes[size / 2] = static_cast<unsigned char>(value + 1U);
    bytes[size - 1] = static_cast<unsigned char>(value + 2U);
}

__attribute__((noinline)) void* AllocateFromPath(size_t size, unsigned char value)
{
    void* ptr = std::malloc(size);
    if (ptr == nullptr) {
        std::cerr << "malloc failed for size=" << size << "\n";
        std::abort();
    }
    TouchMemory(ptr, size, value);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr), std::memory_order_relaxed);
    return ptr;
}

__attribute__((noinline)) void BalancedAllocFreePath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0x11);
    std::free(ptr);
}

__attribute__((noinline)) void DefiniteLeakPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0x22);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 4U, std::memory_order_relaxed);
}

__attribute__((noinline)) void GrowthLeakPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0x33);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 5U, std::memory_order_relaxed);
}

__attribute__((noinline)) void* DelayedAllocPath(size_t size)
{
    return AllocateFromPath(size, 0x44);
}

__attribute__((noinline)) void HighFreqBalancedPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0x55);
    std::free(ptr);
}

__attribute__((noinline)) void MixedLeakPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0x66);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 6U, std::memory_order_relaxed);
}

__attribute__((noinline)) void MixedBalancedPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0x77);
    std::free(ptr);
}

__attribute__((noinline)) void* GameAssetLoadPath(size_t size)
{
    return AllocateFromPath(size, 0x81);
}

__attribute__((noinline)) void GameSceneLeakPath(size_t size)
{
    void* ptr = GameAssetLoadPath(size);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 7U, std::memory_order_relaxed);
}

__attribute__((noinline)) void GameSceneGrowthPath(size_t size)
{
    void* ptr = GameAssetLoadPath(size);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 8U, std::memory_order_relaxed);
}

__attribute__((noinline)) void GameSceneBalancedPath(size_t size)
{
    void* ptr = GameAssetLoadPath(size);
    std::free(ptr);
}

__attribute__((noinline)) void* GameAsyncBufferPath(size_t size)
{
    return AllocateFromPath(size, 0x91);
}

__attribute__((noinline)) void GameFrameTempPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0xa1);
    std::free(ptr);
}

__attribute__((noinline)) void GameUiLeakPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0xb1);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 9U, std::memory_order_relaxed);
}

__attribute__((noinline)) void GameUiBalancedPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0xb2);
    std::free(ptr);
}

__attribute__((noinline)) void* GameNetworkPacketPath(size_t size)
{
    return AllocateFromPath(size, 0xc1);
}

__attribute__((noinline)) void GameNetworkPacketLeakPath(size_t size)
{
    void* ptr = GameNetworkPacketPath(size);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 10U, std::memory_order_relaxed);
}

__attribute__((noinline)) void GameNetworkPacketBalancedPath(size_t size)
{
    void* ptr = GameNetworkPacketPath(size);
    std::free(ptr);
}

__attribute__((noinline)) void* GameAudioDecodePath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0xd1);
    const size_t grown_size = size * 2U;
    void* grown = std::realloc(ptr, grown_size);
    if (grown == nullptr) {
        std::free(ptr);
        std::abort();
    }
    TouchMemory(grown, grown_size, 0xd2);
    return grown;
}

__attribute__((noinline)) void GameAudioDecodeLeakPath(size_t size)
{
    void* ptr = GameAudioDecodePath(size);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 11U, std::memory_order_relaxed);
}

__attribute__((noinline)) void GameAudioDecodeBalancedPath(size_t size)
{
    void* ptr = GameAudioDecodePath(size);
    std::free(ptr);
}

__attribute__((noinline)) void* GameCacheInsertPath(size_t size)
{
    return AllocateFromPath(size, 0xe1);
}

__attribute__((noinline)) void* GameCacheFinalRetainPath(size_t size)
{
    return AllocateFromPath(size, 0xe2);
}

__attribute__((noinline)) void* GameObjectPoolAcquirePath(size_t size)
{
    return AllocateFromPath(size, 0xf1);
}

__attribute__((noinline)) void* GameObjectPoolLeakPath(size_t size)
{
    return AllocateFromPath(size, 0xf2);
}

__attribute__((noinline)) void GameObjectPoolReleasePath(void* ptr)
{
    std::free(ptr);
}

__attribute__((noinline)) void* GameMmapMapPath(size_t size)
{
    void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "mmap failed for size=" << size << "\n";
        std::abort();
    }
    TouchMemory(ptr, size, 0xf3);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 13U, std::memory_order_relaxed);
    return ptr;
}

__attribute__((noinline)) void GameMmapLeakPath(size_t size)
{
    void* ptr = GameMmapMapPath(size);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 14U, std::memory_order_relaxed);
}

__attribute__((noinline)) void GameMmapBalancedPath(size_t size)
{
    void* ptr = GameMmapMapPath(size);
    if (::munmap(ptr, size) != 0) {
        std::abort();
    }
}

__attribute__((noinline)) void GameUiCallbackLeakPath(size_t size)
{
    void* owner = AllocateFromPath(size, 0xf4);
    void* listener = AllocateFromPath(size, 0xf5);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(owner) >> 15U, std::memory_order_relaxed);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(listener) >> 16U, std::memory_order_relaxed);
}

__attribute__((noinline)) void GameUiCallbackBalancedPath(size_t size)
{
    void* owner = AllocateFromPath(size, 0xf6);
    void* listener = AllocateFromPath(size, 0xf7);
    std::free(listener);
    std::free(owner);
}

__attribute__((noinline)) void GameLongRunningTempPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0xf8);
    std::free(ptr);
}

__attribute__((noinline)) void GameLongRunningLeakPath(size_t size)
{
    void* ptr = AllocateFromPath(size, 0xf9);
    g_sink.fetch_xor(reinterpret_cast<uintptr_t>(ptr) >> 17U, std::memory_order_relaxed);
}

void AddAlloc(Stats* stats, size_t size)
{
    ++stats->allocations;
    stats->expected_outstanding_bytes += size;
}

void RunNoLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        BalancedAllocFreePath(config.size);
        ++stats->allocations;
        ++stats->frees;
    }
}

void RunDefiniteLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        DefiniteLeakPath(config.size);
        AddAlloc(stats, config.size);
        ++stats->expected_outstanding_blocks;
    }
}

void RunGrowthLeakWorker(const Config& config, Stats* stats)
{
    const uint64_t rounds = static_cast<uint64_t>(config.rounds);
    const uint64_t per_round = (config.iterations + rounds - 1U) / rounds;
    uint64_t done = 0;
    for (int round = 0; round < config.rounds && done < config.iterations; ++round) {
        const uint64_t todo = std::min<uint64_t>(per_round, config.iterations - done);
        for (uint64_t i = 0; i < todo; ++i) {
            GrowthLeakPath(config.size);
            AddAlloc(stats, config.size);
            ++stats->expected_outstanding_blocks;
        }
        done += todo;
        if (config.delay_ms > 0 && done < config.iterations) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delay_ms));
        }
    }
}

void RunDelayedFreeWorker(const Config& config, Stats* stats)
{
    std::vector<void*> delayed;
    delayed.reserve(static_cast<size_t>(config.iterations));
    for (uint64_t i = 0; i < config.iterations; ++i) {
        void* ptr = DelayedAllocPath(config.size);
        delayed.push_back(ptr);
        ++stats->allocations;
    }
    if (config.delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config.delay_ms));
    }
    for (void* ptr : delayed) {
        std::free(ptr);
        ++stats->frees;
    }
}

void RunHighFreqNoLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        HighFreqBalancedPath(config.size);
        ++stats->allocations;
        ++stats->frees;
    }
}

void RunMixedWorker(const Config& config, Stats* stats)
{
    std::vector<void*> delayed;
    delayed.reserve(static_cast<size_t>(config.iterations / 10U + 1U));
    for (uint64_t i = 0; i < config.iterations; ++i) {
        switch (i % 10U) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
                MixedBalancedPath(config.size);
                ++stats->allocations;
                ++stats->frees;
                break;
            case 6: {
                const size_t leak_size = config.size * 2U;
                MixedLeakPath(leak_size);
                AddAlloc(stats, leak_size);
                ++stats->expected_outstanding_blocks;
                break;
            }
            case 7: {
                void* ptr = DelayedAllocPath(config.size * 3U);
                delayed.push_back(ptr);
                ++stats->allocations;
                break;
            }
            case 8:
                MixedBalancedPath(config.size * 4U);
                ++stats->allocations;
                ++stats->frees;
                break;
            case 9: {
                const size_t leak_size = config.size / 2U + 1U;
                MixedLeakPath(leak_size);
                AddAlloc(stats, leak_size);
                ++stats->expected_outstanding_blocks;
                break;
            }
        }
    }
    if (config.delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config.delay_ms));
    }
    for (void* ptr : delayed) {
        std::free(ptr);
        ++stats->frees;
    }
}

void RunGameSceneNoLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        GameSceneBalancedPath(config.size);
        ++stats->allocations;
        ++stats->frees;
    }
}

void RunGameSceneLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        GameSceneLeakPath(config.size);
        AddAlloc(stats, config.size);
        ++stats->expected_outstanding_blocks;
    }
}

void RunGameSceneGrowthWorker(const Config& config, Stats* stats)
{
    const uint64_t rounds = static_cast<uint64_t>(config.rounds);
    const uint64_t per_round = (config.iterations + rounds - 1U) / rounds;
    uint64_t done = 0;
    for (int round = 0; round < config.rounds && done < config.iterations; ++round) {
        const uint64_t todo = std::min<uint64_t>(per_round, config.iterations - done);
        for (uint64_t i = 0; i < todo; ++i) {
            GameSceneGrowthPath(config.size);
            AddAlloc(stats, config.size);
            ++stats->expected_outstanding_blocks;
        }
        done += todo;
        if (config.delay_ms > 0 && done < config.iterations) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delay_ms));
        }
    }
}

void RunGameAsyncDelayedFreeWorker(const Config& config, Stats* stats)
{
    std::vector<void*> pending;
    pending.reserve(static_cast<size_t>(config.iterations));
    for (uint64_t i = 0; i < config.iterations; ++i) {
        pending.push_back(GameAsyncBufferPath(config.size));
        ++stats->allocations;
    }
    std::thread reclaimer([&pending, &config, stats]() {
        if (config.delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delay_ms));
        }
        for (void* ptr : pending) {
            std::free(ptr);
            ++stats->frees;
        }
    });
    reclaimer.join();
}

void RunGameFrameNoLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        GameFrameTempPath(config.size);
        ++stats->allocations;
        ++stats->frees;
    }
}

void RunGameMixedWorker(const Config& config, Stats* stats)
{
    std::vector<void*> pending;
    pending.reserve(static_cast<size_t>(config.iterations / 10U + 1U));
    for (uint64_t i = 0; i < config.iterations; ++i) {
        switch (i % 10U) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
                GameFrameTempPath(config.size);
                ++stats->allocations;
                ++stats->frees;
                break;
            case 6: {
                const size_t leak_size = config.size * 2U;
                GameUiLeakPath(leak_size);
                AddAlloc(stats, leak_size);
                ++stats->expected_outstanding_blocks;
                break;
            }
            case 7:
                pending.push_back(GameAsyncBufferPath(config.size * 3U));
                ++stats->allocations;
                break;
            case 8:
                GameUiBalancedPath(config.size * 4U);
                ++stats->allocations;
                ++stats->frees;
                break;
            case 9: {
                const size_t leak_size = config.size / 2U + 1U;
                GameUiLeakPath(leak_size);
                AddAlloc(stats, leak_size);
                ++stats->expected_outstanding_blocks;
                break;
            }
        }
    }
    std::thread reclaimer([&pending, &config, stats]() {
        if (config.delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delay_ms));
        }
        for (void* ptr : pending) {
            std::free(ptr);
            ++stats->frees;
        }
    });
    reclaimer.join();
}

void RunGameNetworkPacketWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        if (i % 7U == 3U) {
            GameNetworkPacketLeakPath(config.size);
            AddAlloc(stats, config.size);
            ++stats->expected_outstanding_blocks;
        } else {
            GameNetworkPacketBalancedPath(config.size);
            ++stats->allocations;
            ++stats->frees;
        }
    }
}

void RunGameAudioDecodeWorker(const Config& config, Stats* stats)
{
    const size_t decoded_size = config.size * 2U;
    for (uint64_t i = 0; i < config.iterations; ++i) {
        if (i % 6U == 2U) {
            GameAudioDecodeLeakPath(config.size);
            AddAlloc(stats, decoded_size);
            ++stats->expected_outstanding_blocks;
        } else {
            GameAudioDecodeBalancedPath(config.size);
            ++stats->allocations;
            ++stats->frees;
        }
    }
}

void RunGameCacheEvictionWorker(const Config& config, Stats* stats)
{
    std::vector<void*> cache;
    cache.reserve(4);
    for (uint64_t i = 0; i < config.iterations; ++i) {
        const bool final_retained = (config.iterations - i <= 3U) && (i % 3U == 0U);
        void* ptr = final_retained ? GameCacheFinalRetainPath(config.size) : GameCacheInsertPath(config.size);
        ++stats->allocations;
        if (i % 3U == 0U) {
            cache.push_back(ptr);
            if (cache.size() > 4U) {
                std::free(cache.front());
                cache.erase(cache.begin());
                ++stats->frees;
            }
        } else {
            std::free(ptr);
            ++stats->frees;
        }
    }
    for (size_t i = 0; i + 1U < cache.size(); ++i) {
        std::free(cache[i]);
        ++stats->frees;
    }
    if (!cache.empty()) {
        void* retained = cache.back();
        g_sink.fetch_xor(reinterpret_cast<uintptr_t>(retained) >> 12U, std::memory_order_relaxed);
        ++stats->expected_outstanding_blocks;
        stats->expected_outstanding_bytes += config.size;
    }
}

void RunGameObjectPoolNoLeakWorker(const Config& config, Stats* stats)
{
    std::vector<void*> pool;
    pool.reserve(4);
    for (uint64_t i = 0; i < config.iterations; ++i) {
        pool.push_back(GameObjectPoolAcquirePath(config.size));
        ++stats->allocations;
        if (pool.size() == 4U) {
            for (void* ptr : pool) {
                GameObjectPoolReleasePath(ptr);
                ++stats->frees;
            }
            pool.clear();
        }
    }
    for (void* ptr : pool) {
        GameObjectPoolReleasePath(ptr);
        ++stats->frees;
    }
}

void RunGameObjectPoolLeakWorker(const Config& config, Stats* stats)
{
    std::vector<void*> retained;
    retained.reserve(static_cast<size_t>(config.iterations / 8U + 1U));
    for (uint64_t i = 0; i < config.iterations; ++i) {
        if (i % 8U == 3U) {
            void* ptr = GameObjectPoolLeakPath(config.size);
            retained.push_back(ptr);
            AddAlloc(stats, config.size);
            ++stats->expected_outstanding_blocks;
        } else {
            void* ptr = GameObjectPoolAcquirePath(config.size);
            GameObjectPoolReleasePath(ptr);
            ++stats->allocations;
            ++stats->frees;
        }
    }
}

void RunGameMmapNoLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        GameMmapBalancedPath(config.size);
        ++stats->allocations;
        ++stats->frees;
    }
}

void RunGameMmapLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        GameMmapLeakPath(config.size);
        AddAlloc(stats, config.size);
        ++stats->expected_outstanding_blocks;
    }
}

void RunGameUiCallbackNoLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        GameUiCallbackBalancedPath(config.size);
        stats->allocations += 2U;
        stats->frees += 2U;
    }
}

void RunGameUiCallbackLeakWorker(const Config& config, Stats* stats)
{
    for (uint64_t i = 0; i < config.iterations; ++i) {
        GameUiCallbackLeakPath(config.size);
        stats->allocations += 2U;
        stats->expected_outstanding_blocks += 2U;
        stats->expected_outstanding_bytes += config.size * 2U;
    }
}

void RunGameLongRunningNoLeakWorker(const Config& config, Stats* stats)
{
    for (int round = 0; round < config.rounds; ++round) {
        for (uint64_t i = 0; i < config.iterations; ++i) {
            GameLongRunningTempPath(config.size);
            ++stats->allocations;
            ++stats->frees;
        }
        if (config.delay_ms > 0 && round + 1 < config.rounds) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delay_ms));
        }
    }
}

void RunGameLongRunningLeakWorker(const Config& config, Stats* stats)
{
    for (int round = 0; round < config.rounds; ++round) {
        for (uint64_t i = 0; i < config.iterations; ++i) {
            GameLongRunningLeakPath(config.size);
            AddAlloc(stats, config.size);
            ++stats->expected_outstanding_blocks;
        }
        if (config.delay_ms > 0 && round + 1 < config.rounds) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delay_ms));
        }
    }
}

Stats RunCase(const Config& config)
{
    std::vector<Stats> per_thread(static_cast<size_t>(config.threads));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(config.threads));
    for (int i = 0; i < config.threads; ++i) {
        workers.emplace_back([&config, &per_thread, i]() {
            Stats& stats = per_thread[static_cast<size_t>(i)];
            switch (config.benchmark_case) {
                case BenchmarkCase::kNoLeak:
                    RunNoLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kDefiniteLeak:
                    RunDefiniteLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGrowthLeak:
                    RunGrowthLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kDelayedFree:
                    RunDelayedFreeWorker(config, &stats);
                    break;
                case BenchmarkCase::kHighFreqNoLeak:
                    RunHighFreqNoLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kMixed:
                    RunMixedWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameSceneNoLeak:
                    RunGameSceneNoLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameSceneLeak:
                    RunGameSceneLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameSceneGrowth:
                    RunGameSceneGrowthWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameAsyncDelayedFree:
                    RunGameAsyncDelayedFreeWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameFrameNoLeak:
                    RunGameFrameNoLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameMixed:
                    RunGameMixedWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameNetworkPacket:
                    RunGameNetworkPacketWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameAudioDecode:
                    RunGameAudioDecodeWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameCacheEviction:
                    RunGameCacheEvictionWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameObjectPoolNoLeak:
                    RunGameObjectPoolNoLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameObjectPoolLeak:
                    RunGameObjectPoolLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameMmapNoLeak:
                    RunGameMmapNoLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameMmapLeak:
                    RunGameMmapLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameUiCallbackNoLeak:
                    RunGameUiCallbackNoLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameUiCallbackLeak:
                    RunGameUiCallbackLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameLongRunningNoLeak:
                    RunGameLongRunningNoLeakWorker(config, &stats);
                    break;
                case BenchmarkCase::kGameLongRunningLeak:
                    RunGameLongRunningLeakWorker(config, &stats);
                    break;
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    Stats total;
    for (const Stats& stats : per_thread) {
        total.allocations += stats.allocations;
        total.frees += stats.frees;
        total.expected_outstanding_blocks += stats.expected_outstanding_blocks;
        total.expected_outstanding_bytes += stats.expected_outstanding_bytes;
    }
    const auto add_checkpoint = [&total](const std::string& name, uint64_t blocks, uint64_t bytes) {
        total.truth_checkpoints.push_back({name, blocks, bytes});
    };
    switch (config.benchmark_case) {
        case BenchmarkCase::kGrowthLeak:
        case BenchmarkCase::kGameSceneGrowth: {
            const uint64_t rounds = static_cast<uint64_t>(config.rounds);
            const uint64_t per_round = (config.iterations + rounds - 1U) / rounds;
            uint64_t done = 0;
            for (int round = 0; round < config.rounds && done < config.iterations; ++round) {
                done += std::min<uint64_t>(per_round, config.iterations - done);
                const uint64_t blocks = static_cast<uint64_t>(config.threads) * done;
                add_checkpoint("round_" + std::to_string(round + 1), blocks, blocks * config.size);
            }
            break;
        }
        case BenchmarkCase::kDelayedFree:
        case BenchmarkCase::kGameAsyncDelayedFree:
            add_checkpoint("after_alloc",
                static_cast<uint64_t>(config.threads) * config.iterations,
                static_cast<uint64_t>(config.threads) * config.iterations * config.size);
            add_checkpoint("after_free", 0, 0);
            break;
        case BenchmarkCase::kGameCacheEviction:
            add_checkpoint("after_eviction",
                total.expected_outstanding_blocks, total.expected_outstanding_bytes);
            break;
        case BenchmarkCase::kGameObjectPoolNoLeak:
        case BenchmarkCase::kGameObjectPoolLeak:
            add_checkpoint("after_load",
                total.expected_outstanding_blocks, total.expected_outstanding_bytes);
            add_checkpoint("after_cycle",
                total.expected_outstanding_blocks, total.expected_outstanding_bytes);
            break;
        case BenchmarkCase::kGameMmapNoLeak:
        case BenchmarkCase::kGameMmapLeak:
            add_checkpoint("after_load",
                total.expected_outstanding_blocks, total.expected_outstanding_bytes);
            add_checkpoint("after_unmap",
                total.expected_outstanding_blocks, total.expected_outstanding_bytes);
            break;
        case BenchmarkCase::kGameUiCallbackNoLeak:
        case BenchmarkCase::kGameUiCallbackLeak:
            add_checkpoint("after_load",
                total.expected_outstanding_blocks, total.expected_outstanding_bytes);
            add_checkpoint("after_cancel",
                total.expected_outstanding_blocks, total.expected_outstanding_bytes);
            break;
        case BenchmarkCase::kGameLongRunningNoLeak:
        case BenchmarkCase::kGameLongRunningLeak: {
            const bool leaking = config.benchmark_case == BenchmarkCase::kGameLongRunningLeak;
            for (int round = 0; round < config.rounds; ++round) {
                const uint64_t blocks = leaking
                    ? static_cast<uint64_t>(config.threads) * config.iterations * static_cast<uint64_t>(round + 1)
                    : 0;
                add_checkpoint("after_cycle_" + std::to_string(round + 1),
                    blocks, blocks * config.size);
            }
            break;
        }
        default:
            add_checkpoint("after_workload",
                total.expected_outstanding_blocks, total.expected_outstanding_bytes);
            break;
    }
    return total;
}

std::string JsonEscape(const char* text)
{
    std::ostringstream out;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\\') {
            out << '\\' << *p;
        } else if (*p == '\n') {
            out << "\\n";
        } else {
            out << *p;
        }
    }
    return out.str();
}

void PrintJson(const Config& config, const Stats& stats, double elapsed_seconds)
{
    const CaseInfo info = GetCaseInfo(config.benchmark_case);
    const double allocs_per_second = elapsed_seconds > 0.0
        ? static_cast<double>(stats.allocations) / elapsed_seconds : 0.0;
    std::cout << std::fixed << std::setprecision(6)
              << "{\n"
              << "  \"case\": \"" << info.name << "\",\n"
              << "  \"purpose\": \"" << JsonEscape(info.purpose) << "\",\n"
              << "  \"suite_group\": \"" << info.suite_group << "\",\n"
              << "  \"intentional_leak\": " << (info.intentionally_leaky ? "true" : "false") << ",\n"
              << "  \"threads\": " << config.threads << ",\n"
              << "  \"iterations_per_thread\": " << config.iterations << ",\n"
              << "  \"allocation_size\": " << config.size << ",\n"
              << "  \"rounds\": " << config.rounds << ",\n"
              << "  \"start_delay_ms\": " << config.start_delay_ms << ",\n"
              << "  \"delay_ms\": " << config.delay_ms << ",\n"
              << "  \"post_delay_ms\": " << config.post_delay_ms << ",\n"
              << "  \"allocations\": " << stats.allocations << ",\n"
              << "  \"frees\": " << stats.frees << ",\n"
              << "  \"expected_outstanding_blocks\": " << stats.expected_outstanding_blocks << ",\n"
              << "  \"expected_outstanding_bytes\": " << stats.expected_outstanding_bytes << ",\n"
              << "  \"truth_checkpoints\": [\n";
    for (size_t i = 0; i < stats.truth_checkpoints.size(); ++i) {
        const TruthCheckpoint& checkpoint = stats.truth_checkpoints[i];
        std::cout << "    {\"name\": \"" << JsonEscape(checkpoint.name.c_str())
                  << "\", \"outstanding_blocks\": " << checkpoint.outstanding_blocks
                  << ", \"outstanding_bytes\": " << checkpoint.outstanding_bytes << "}"
                  << (i + 1 == stats.truth_checkpoints.size() ? "\n" : ",\n");
    }
    std::cout << "  ],\n"
              << "  \"elapsed_seconds\": " << elapsed_seconds << ",\n"
              << "  \"allocations_per_second\": " << allocs_per_second << ",\n"
              << "  \"sink\": " << g_sink.load(std::memory_order_relaxed) << "\n"
              << "}\n";
}

void PrintText(const Config& config, const Stats& stats, double elapsed_seconds)
{
    const CaseInfo info = GetCaseInfo(config.benchmark_case);
    const double allocs_per_second = elapsed_seconds > 0.0
        ? static_cast<double>(stats.allocations) / elapsed_seconds : 0.0;
    std::cout << std::fixed << std::setprecision(6)
              << "case=" << info.name
              << " purpose=\"" << info.purpose << "\""
              << " suite_group=" << info.suite_group
              << " intentional_leak=" << (info.intentionally_leaky ? "true" : "false")
              << " threads=" << config.threads
              << " iterations_per_thread=" << config.iterations
              << " allocation_size=" << config.size
              << " start_delay_ms=" << config.start_delay_ms
              << " allocations=" << stats.allocations
              << " frees=" << stats.frees
              << " expected_outstanding_blocks=" << stats.expected_outstanding_blocks
              << " expected_outstanding_bytes=" << stats.expected_outstanding_bytes
              << " elapsed_seconds=" << elapsed_seconds
              << " allocations_per_second=" << allocs_per_second
              << " sink=" << g_sink.load(std::memory_order_relaxed)
              << "\n";
}

bool CheckExpectedValue(const char* name, uint64_t actual, uint64_t expected)
{
    if (actual == expected) {
        return true;
    }
    std::cerr << "expectation mismatch: " << name
              << " actual=" << actual
              << " expected=" << expected
              << "\n";
    return false;
}

bool ValidateExpectations(const Config& config, const Stats& stats)
{
    bool ok = true;
    if (config.has_expect_allocations) {
        ok = CheckExpectedValue("allocations", stats.allocations, config.expect_allocations) && ok;
    }
    if (config.has_expect_frees) {
        ok = CheckExpectedValue("frees", stats.frees, config.expect_frees) && ok;
    }
    if (config.has_expect_outstanding_blocks) {
        ok = CheckExpectedValue("expected_outstanding_blocks",
            stats.expected_outstanding_blocks, config.expect_outstanding_blocks) && ok;
    }
    if (config.has_expect_outstanding_bytes) {
        ok = CheckExpectedValue("expected_outstanding_bytes",
            stats.expected_outstanding_bytes, config.expect_outstanding_bytes) && ok;
    }
    return ok;
}

}  // namespace

int main(int argc, char* argv[])
{
    Config config;
    if (!ParseArgs(argc, argv, &config)) {
        PrintUsage();
        return 2;
    }

    if (config.start_delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config.start_delay_ms));
    }
    const auto start = std::chrono::steady_clock::now();
    const Stats stats = RunCase(config);
    if (config.post_delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config.post_delay_ms));
    }
    const auto end = std::chrono::steady_clock::now();
    const double elapsed_seconds = std::chrono::duration<double>(end - start).count();

    if (config.json) {
        PrintJson(config, stats, elapsed_seconds);
    } else {
        PrintText(config, stats, elapsed_seconds);
    }
    return ValidateExpectations(config, stats) ? 0 : 3;
}
