#include <cstddef>
#include <cstdint>

#include "producer_hook/ablation_config.h"
#include "producer_hook/hook_guard.h"
#include "producer_hook/hook_writer.h"
#include "producer_hook/hotpath_profile.h"

extern "C" void* __libc_malloc(size_t size);
extern "C" void __libc_free(void* ptr);
extern "C" void* __libc_calloc(size_t nmemb, size_t size);
extern "C" void* __libc_realloc(void* ptr, size_t size);

extern "C" void* malloc(size_t size)
{
    void* ptr = __libc_malloc(size);
    const uint64_t hook_start = linux_native_hook_v1::HotpathProfileStart();
    const int stage = linux_native_hook_v1::GetAblationStage();
    if (stage <= linux_native_hook_v1::kAblationStageHookEntry) {
        linux_native_hook_v1::HotpathProfileAdd(
            linux_native_hook_v1::HotpathProfileSegment::kHookEntry, hook_start);
        return ptr;
    }
    linux_native_hook_v1::HotpathProfileAdd(
        linux_native_hook_v1::HotpathProfileSegment::kHookEntry, hook_start);

    const uint64_t guard_start = linux_native_hook_v1::HotpathProfileStart();
    linux_native_hook_v1::HookReentryGuard guard;
    linux_native_hook_v1::HotpathProfileAdd(
        linux_native_hook_v1::HotpathProfileSegment::kGuard, guard_start);
    if (guard.active() && stage >= linux_native_hook_v1::kAblationStageMutex) {
        linux_native_hook_v1::HookWriter::Instance().RecordAlloc(ptr, size);
    }
    return ptr;
}

extern "C" void free(void* ptr)
{
    const uint64_t hook_start = linux_native_hook_v1::HotpathProfileStart();
    const int stage = linux_native_hook_v1::GetAblationStage();
    if (stage <= linux_native_hook_v1::kAblationStageHookEntry) {
        linux_native_hook_v1::HotpathProfileAdd(
            linux_native_hook_v1::HotpathProfileSegment::kHookEntry, hook_start);
        __libc_free(ptr);
        return;
    }
    linux_native_hook_v1::HotpathProfileAdd(
        linux_native_hook_v1::HotpathProfileSegment::kHookEntry, hook_start);

    const uint64_t guard_start = linux_native_hook_v1::HotpathProfileStart();
    linux_native_hook_v1::HookReentryGuard guard;
    linux_native_hook_v1::HotpathProfileAdd(
        linux_native_hook_v1::HotpathProfileSegment::kGuard, guard_start);
    if (guard.active() && stage >= linux_native_hook_v1::kAblationStageMutex) {
        linux_native_hook_v1::HookWriter::Instance().RecordFree(ptr);
    }
    __libc_free(ptr);
}

extern "C" void* calloc(size_t nmemb, size_t size)
{
    void* ptr = __libc_calloc(nmemb, size);
    const uint64_t hook_start = linux_native_hook_v1::HotpathProfileStart();
    const int stage = linux_native_hook_v1::GetAblationStage();
    if (stage <= linux_native_hook_v1::kAblationStageHookEntry) {
        linux_native_hook_v1::HotpathProfileAdd(
            linux_native_hook_v1::HotpathProfileSegment::kHookEntry, hook_start);
        return ptr;
    }
    linux_native_hook_v1::HotpathProfileAdd(
        linux_native_hook_v1::HotpathProfileSegment::kHookEntry, hook_start);

    const uint64_t guard_start = linux_native_hook_v1::HotpathProfileStart();
    linux_native_hook_v1::HookReentryGuard guard;
    linux_native_hook_v1::HotpathProfileAdd(
        linux_native_hook_v1::HotpathProfileSegment::kGuard, guard_start);
    if (guard.active() && stage >= linux_native_hook_v1::kAblationStageMutex) {
        linux_native_hook_v1::HookWriter::Instance().RecordAlloc(ptr, nmemb * size);
    }
    return ptr;
}

extern "C" void* realloc(void* ptr, size_t size)
{
    void* new_ptr = __libc_realloc(ptr, size);
    const uint64_t hook_start = linux_native_hook_v1::HotpathProfileStart();
    const int stage = linux_native_hook_v1::GetAblationStage();
    if (stage <= linux_native_hook_v1::kAblationStageHookEntry) {
        linux_native_hook_v1::HotpathProfileAdd(
            linux_native_hook_v1::HotpathProfileSegment::kHookEntry, hook_start);
        return new_ptr;
    }
    linux_native_hook_v1::HotpathProfileAdd(
        linux_native_hook_v1::HotpathProfileSegment::kHookEntry, hook_start);

    const uint64_t guard_start = linux_native_hook_v1::HotpathProfileStart();
    linux_native_hook_v1::HookReentryGuard guard;
    linux_native_hook_v1::HotpathProfileAdd(
        linux_native_hook_v1::HotpathProfileSegment::kGuard, guard_start);
    if (guard.active() && stage >= linux_native_hook_v1::kAblationStageMutex) {
        linux_native_hook_v1::HookWriter::Instance().RecordAlloc(new_ptr, size);
    }
    return new_ptr;
}
