#pragma once

namespace linux_native_hook_v1 {

class HookReentryGuard {
public:
    HookReentryGuard();
    ~HookReentryGuard();

    bool active() const { return active_; }

    static bool IsInsideHook();

private:
    bool active_ = false;
};

}  // namespace linux_native_hook_v1
