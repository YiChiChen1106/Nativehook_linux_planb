#include <cerrno>
#include <csignal>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>

#include <sys/eventfd.h>
#include <unistd.h>

#include "common/unix_defs.h"
#include "consumer/control_server.h"
#include "consumer/metrics.h"
#include "consumer/shm_consumer.h"

namespace linux_native_hook_v1 {
namespace {

volatile std::sig_atomic_t g_keep_running = 1;

void HandleSignal(int)
{
    g_keep_running = 0;
}

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

bool ParseI32(const char* text, int32_t* out_value)
{
    if (text == nullptr || out_value == nullptr) {
        return false;
    }
    char* end_ptr = nullptr;
    const long value = std::strtol(text, &end_ptr, 10);
    if (*text == '\0' || end_ptr == nullptr || *end_ptr != '\0' || value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    *out_value = static_cast<int32_t>(value);
    return true;
}

void PrintUsage()
{
    std::printf(
        "Usage: consumer [--socket path] [--shm name] [--capacity records] [--flush-threshold n] "
        "[--sample-interval n] [--filter-size bytes] [--blocked] [--verbose] [--profile] [--shards n]\n");
}

std::string BuildShmName(int32_t peer_pid)
{
    std::ostringstream oss;
    oss << "/hooknativesmb_" << peer_pid << ":0";
    return oss.str();
}

}  // namespace
}  // namespace linux_native_hook_v1

int main(int argc, char* argv[])
{
    using namespace linux_native_hook_v1;

    std::string socket_path = kDefaultSocketPath;
    std::string shm_name = kDefaultShmName;
    uint32_t capacity = kDefaultRingCapacity;
    uint32_t flush_threshold = kDefaultFlushThreshold;
    uint32_t sample_interval = kDefaultSampleInterval;
    int32_t filter_size = kDefaultFilterSize;
    uint8_t is_blocked = 0;
    bool verbose = false;
    bool profile = false;
    uint32_t num_shards = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (arg == "--shm" && i + 1 < argc) {
            shm_name = argv[++i];
        } else if (arg == "--capacity" && i + 1 < argc) {
            if (!ParsePositiveU32(argv[++i], &capacity)) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--flush-threshold" && i + 1 < argc) {
            if (!ParsePositiveU32(argv[++i], &flush_threshold)) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--sample-interval" && i + 1 < argc) {
            if (!ParsePositiveU32(argv[++i], &sample_interval)) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--filter-size" && i + 1 < argc) {
            if (!ParseI32(argv[++i], &filter_size)) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--blocked") {
            is_blocked = 1;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--profile") {
            profile = true;
        } else if (arg == "--shards" && i + 1 < argc) {
            if (!ParsePositiveU32(argv[++i], &num_shards) || num_shards > kShmMaxShards) {
                PrintUsage();
                return 1;
            }
        } else {
            PrintUsage();
            return 1;
        }
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    ControlServer server(socket_path);
    if (!server.Start()) {
        std::fprintf(stderr, "Failed to start control server at %s\n", socket_path.c_str());
        return 1;
    }

    std::printf("consumer listening on %s\n", socket_path.c_str());
    std::fflush(stdout);

    int32_t peer_pid = -1;
    const int client_fd = server.AcceptClientAndReadPid(&peer_pid);
    if (client_fd < 0) {
        std::fprintf(stderr, "Failed to accept/read client pid\n");
        return 1;
    }

    if (shm_name == kDefaultShmName) {
        shm_name = BuildShmName(peer_pid);
    }

    ShmConsumer consumer;
    if (!consumer.CreateAndMap(shm_name, capacity, num_shards)) {
        std::fprintf(stderr, "Failed to create shared memory: %s\n", std::strerror(errno));
        close(client_fd);
        return 1;
    }

    const int event_fd = eventfd(0, 0);
    if (event_fd < 0) {
        std::fprintf(stderr, "Failed to create eventfd: %s\n", std::strerror(errno));
        close(client_fd);
        return 1;
    }

    SharedResources resources;
    resources.shm_fd = consumer.shm_fd();
    resources.event_fd = event_fd;
    resources.config.ring_capacity = consumer.capacity();
    resources.config.flush_threshold = flush_threshold;
    resources.config.sample_interval = sample_interval;
    resources.config.clock_id = CLOCK_REALTIME;
    resources.config.filter_size = filter_size;
    resources.config.max_stack_depth = kDefaultMaxStackDepth;
    resources.config.is_blocked = is_blocked;

    if (!server.SendResources(client_fd, resources)) {
        std::fprintf(stderr, "Failed to send resources to client\n");
        close(client_fd);
        close(event_fd);
        return 1;
    }

    std::printf("shared memory name=%s capacity=%u\n", shm_name.c_str(), consumer.capacity());
    std::printf("flush_threshold=%u sample_interval=%u filter_size=%d max_stack_depth=%u clock_id=%d is_blocked=%u\n",
        resources.config.flush_threshold,
        resources.config.sample_interval,
        resources.config.filter_size,
        resources.config.max_stack_depth,
        resources.config.clock_id,
        resources.config.is_blocked);
    std::fflush(stdout);

    std::printf("client connected, waiting for eventfd notifications\n");
    std::fflush(stdout);

    Metrics metrics;
    timespec t0 {}, t1 {}, t2 {}, t3 {};
    while (g_keep_running != 0) {
        if (profile) {
            clock_gettime(CLOCK_MONOTONIC, &t0);
        }

        uint64_t wake_count = 0;
        const ssize_t read_bytes = read(event_fd, &wake_count, sizeof(wake_count));
        if (read_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::fprintf(stderr, "eventfd read failed: %s\n", std::strerror(errno));
            break;
        }
        if (read_bytes == 0) {
            continue;
        }

        if (profile) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
        }

        if (!consumer.ConsumeAvailable(&metrics, verbose)) {
            std::fprintf(stderr, "consumer failed to drain shared memory\n");
            break;
        }

        if (profile) {
            clock_gettime(CLOCK_MONOTONIC, &t2);
        }

        std::printf("wake=%llu %s\n",
            static_cast<unsigned long long>(wake_count),
            metrics.Snapshot().c_str());

        if (profile) {
            clock_gettime(CLOCK_MONOTONIC, &t3);
            const long eventfd_ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
            const long drain_ns = (t2.tv_sec - t1.tv_sec) * 1000000000L + (t2.tv_nsec - t1.tv_nsec);
            const long output_ns = (t3.tv_sec - t2.tv_sec) * 1000000000L + (t3.tv_nsec - t2.tv_nsec);
            std::printf("profile eventfd_ns=%ld drain_ns=%ld output_ns=%ld\n",
                eventfd_ns, drain_ns, output_ns);
        }

        std::fflush(stdout);
    }

    close(client_fd);
    close(event_fd);
    return 0;
}
