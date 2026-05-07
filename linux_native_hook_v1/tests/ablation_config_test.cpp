#include "producer_hook/ablation_config.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: ablation_config_test <0|1>\n";
        return 2;
    }

    const bool expected = std::atoi(argv[1]) != 0;
    const bool actual = linux_native_hook_v1::GetPidTidCacheEnabled();
    if (actual != expected) {
        std::cerr << "expected GetPidTidCacheEnabled()=" << expected << " actual=" << actual << "\n";
        return 1;
    }

    return 0;
}
