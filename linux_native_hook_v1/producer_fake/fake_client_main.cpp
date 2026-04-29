#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "common/unix_defs.h"
#include "producer_fake/fake_writer.h"

namespace linux_native_hook_v1 {
namespace {

bool ParsePositiveU32(const char* text, uint32_t* out_value)
{
    if (text == nullptr || out_value == nullptr) {
        return false;
    }
    char* end_ptr = nullptr;
    unsigned long value = std::strtoul(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || value == 0 || value > UINT32_MAX) {
        return false;
    }
    *out_value = static_cast<uint32_t>(value);
    return true;
}

void PrintUsage()
{
    std::printf("Usage: producer_fake [--socket path] [--bursts count] [--burst-size count] [--sleep-ms n]\n");
}

}  // namespace
}  // namespace linux_native_hook_v1

int main(int argc, char* argv[])
{
    using namespace linux_native_hook_v1;

    std::string socket_path = kDefaultSocketPath;
    uint32_t bursts = 50;
    uint32_t burst_size = kDefaultBurstCount;
    uint32_t sleep_ms = 100;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (arg == "--bursts" && i + 1 < argc) {
            if (!ParsePositiveU32(argv[++i], &bursts)) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--burst-size" && i + 1 < argc) {
            if (!ParsePositiveU32(argv[++i], &burst_size)) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--sleep-ms" && i + 1 < argc) {
            if (!ParsePositiveU32(argv[++i], &sleep_ms)) {
                PrintUsage();
                return 1;
            }
        } else {
            PrintUsage();
            return 1;
        }
    }

    FakeWriter writer;
    if (!writer.Connect(socket_path)) {
        std::fprintf(stderr, "producer_fake failed to connect to %s\n", socket_path.c_str());
        return 1;
    }

    for (uint32_t i = 0; i < bursts; ++i) {
        if (!writer.WriteBurst(burst_size)) {
            std::fprintf(stderr, "producer_fake failed on burst %u\n", i);
            return 1;
        }
        std::printf("producer_fake wrote burst %u/%u\n", i + 1, bursts);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    return 0;
}
