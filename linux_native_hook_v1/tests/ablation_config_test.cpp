#include "common/shm_layout.h"
#include "producer_hook/ablation_config.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: ablation_config_test <pid_tid_cache|sub_ablation|tracking_mode|hotpath_profile|shm_layout> <expected>\n";
        return 2;
    }

    const std::string mode = argv[1];
    if (mode == "shm_layout") {
        if (argc != 4) {
            std::cerr << "usage: ablation_config_test shm_layout <write_index|read_index|dropped|size> <expected>\n";
            return 2;
        }

        const std::string field = argv[2];
        const size_t expected = static_cast<size_t>(std::strtoul(argv[3], nullptr, 10));
        size_t actual = 0;
        if (field == "write_index") {
            actual = offsetof(linux_native_hook_v1::ShmHeader, write_index);
        } else if (field == "read_index") {
            actual = offsetof(linux_native_hook_v1::ShmHeader, read_index);
        } else if (field == "dropped") {
            actual = offsetof(linux_native_hook_v1::ShmHeader, dropped);
        } else if (field == "size") {
            actual = sizeof(linux_native_hook_v1::ShmHeader);
        } else {
            std::cerr << "unknown shm_layout field: " << field << "\n";
            return 2;
        }

        if (actual != expected) {
            std::cerr << "expected " << field << "=" << expected << " actual=" << actual << "\n";
            return 1;
        }
        return 0;
    }

    if (argc != 3) {
        std::cerr << "usage: ablation_config_test <pid_tid_cache|sub_ablation|tracking_mode|hotpath_profile|shm_layout> <expected>\n";
        return 2;
    }
    if (mode == "pid_tid_cache") {
        const bool expected = std::atoi(argv[2]) != 0;
        const bool actual = linux_native_hook_v1::GetPidTidCacheEnabled();
        if (actual != expected) {
            std::cerr << "expected GetPidTidCacheEnabled()=" << expected << " actual=" << actual << "\n";
            return 1;
        }
        return 0;
    }

    if (mode == "sub_ablation") {
        const int expected = std::atoi(argv[2]);
        const int actual = linux_native_hook_v1::GetSubAblationStage();
        if (actual != expected) {
            std::cerr << "expected GetSubAblationStage()=" << expected << " actual=" << actual << "\n";
            return 1;
        }
        return 0;
    }

    if (mode == "tracking_mode") {
        const int expected = std::atoi(argv[2]);
        const int actual = linux_native_hook_v1::GetTrackingMode();
        if (actual != expected) {
            std::cerr << "expected GetTrackingMode()=" << expected << " actual=" << actual << "\n";
            return 1;
        }
        return 0;
    }

    if (mode == "hotpath_profile") {
        const bool expected = std::atoi(argv[2]) != 0;
        const bool actual = linux_native_hook_v1::GetHotpathProfileEnabled();
        if (actual != expected) {
            std::cerr << "expected GetHotpathProfileEnabled()=" << expected << " actual=" << actual << "\n";
            return 1;
        }
        return 0;
    }

    std::cerr << "unknown mode: " << mode << "\n";
    return 2;
}
