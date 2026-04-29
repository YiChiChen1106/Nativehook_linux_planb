#pragma once

#include <string>

#include "common/unix_defs.h"

namespace linux_native_hook_v1 {

struct SharedResources {
    int shm_fd = -1;
    int event_fd = -1;
    ControlConfig config {};
};

class ControlServer {
public:
    explicit ControlServer(std::string socket_path);
    ~ControlServer();

    bool Start();
    int AcceptClientAndReadPid(int32_t* peer_pid);
    bool SendResources(int client_fd, const SharedResources& resources);

    const std::string& socket_path() const { return socket_path_; }

private:
    std::string socket_path_;
    int server_fd_ = -1;
};

}  // namespace linux_native_hook_v1
