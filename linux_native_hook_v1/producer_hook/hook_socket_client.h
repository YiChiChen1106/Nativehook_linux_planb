#pragma once

#include <atomic>
#include <cstdint>
#include <pthread.h>

namespace linux_native_hook_v1 {

// Thin structural equivalent of real OH's weakClient.lock().
// Protects connection lifecycle (reconnect, fd validity).
// The real OH code takes this lock before calling SendStackWithPayload;
// our prototype previously used an atomic fast-path (no lock).
// Adding this layer enables ablation measurement of the client-lock cost.
class HookSocketClient {
public:
    HookSocketClient() { pthread_mutex_init(&mutex_, nullptr); }
    ~HookSocketClient() { pthread_mutex_destroy(&mutex_); }

    void Lock() { pthread_mutex_lock(&mutex_); }
    void Unlock() { pthread_mutex_unlock(&mutex_); }

    bool IsConnected() const
    {
        return connected_.load(std::memory_order_acquire);
    }

    void SetConnected(bool value)
    {
        connected_.store(value, std::memory_order_release);
    }

private:
    pthread_mutex_t mutex_;
    std::atomic<bool> connected_{false};
};

}  // namespace linux_native_hook_v1
