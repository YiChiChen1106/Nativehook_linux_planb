#pragma once

#include <cstdint>

#include "common/hook_record.h"

namespace linux_native_hook_v1 {

struct CapturedStack {
    uint32_t stack_id = 0;
    uint16_t depth = 0;
    uint64_t frames[kMaxStackFrames] = {0};
};

bool StackCaptureEnabled();
CapturedStack CaptureStack();

}  // namespace linux_native_hook_v1
