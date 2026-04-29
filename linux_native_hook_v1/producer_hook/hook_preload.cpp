#include <cstddef>
#include <cstdint>

#include "producer_hook/hook_guard.h"
#include "producer_hook/hook_writer.h"

extern "C" void* __libc_malloc(size_t size);
extern "C" void __libc_free(void* ptr);
extern "C" void* __libc_calloc(size_t nmemb, size_t size);
extern "C" void* __libc_realloc(void* ptr, size_t size);

extern "C" void* malloc(size_t size)
{
    void* ptr = __libc_malloc(size);
    linux_native_hook_v1::HookReentryGuard guard;
    if (guard.active()) {
        linux_native_hook_v1::HookWriter::Instance().RecordAlloc(ptr, size);
    }
    return ptr;
}

extern "C" void free(void* ptr)
{
    linux_native_hook_v1::HookReentryGuard guard;
    if (guard.active()) {
        linux_native_hook_v1::HookWriter::Instance().RecordFree(ptr);
    }
    __libc_free(ptr);
}

extern "C" void* calloc(size_t nmemb, size_t size)
{
    void* ptr = __libc_calloc(nmemb, size);
    linux_native_hook_v1::HookReentryGuard guard;
    if (guard.active()) {
        linux_native_hook_v1::HookWriter::Instance().RecordAlloc(ptr, nmemb * size);
    }
    return ptr;
}

extern "C" void* realloc(void* ptr, size_t size)
{
    void* new_ptr = __libc_realloc(ptr, size);
    linux_native_hook_v1::HookReentryGuard guard;
    if (guard.active()) {
        linux_native_hook_v1::HookWriter::Instance().RecordAlloc(new_ptr, size);
    }
    return new_ptr;
}
