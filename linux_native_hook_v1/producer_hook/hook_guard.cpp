#include "producer_hook/hook_guard.h"

namespace linux_native_hook_v1 {

namespace {

thread_local int g_hook_depth = 0;

}  // namespace

HookReentryGuard::HookReentryGuard()
{
    if (g_hook_depth == 0) {
        active_ = true;
    }
    ++g_hook_depth;
}

HookReentryGuard::~HookReentryGuard()
{
    if (g_hook_depth > 0) {
        --g_hook_depth;
    }
}

bool HookReentryGuard::IsInsideHook()
{
    return g_hook_depth > 0;
}

}  // namespace linux_native_hook_v1
