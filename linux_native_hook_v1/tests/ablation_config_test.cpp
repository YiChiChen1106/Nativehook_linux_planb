#include "producer_hook/ablation_config.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: ablation_config_test <pid_tid_cache|sub_ablation|tracking_mode> <expected>\n";
        return 2;
    }

    const std::string mode = argv[1];
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

    std::cerr << "unknown mode: " << mode << "\n";
    return 2;
}
