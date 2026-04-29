#include "consumer/control_server.h"

#include <cstdio>

#include <unistd.h>

#include "common/socket_fd.h"

namespace linux_native_hook_v1 {

ControlServer::ControlServer(std::string socket_path)
    : socket_path_(std::move(socket_path))
{
}

ControlServer::~ControlServer()
{
    if (server_fd_ >= 0) {
        close(server_fd_);
    }
    unlink(socket_path_.c_str());
}

bool ControlServer::Start()
{
    server_fd_ = CreateUnixServerSocket(socket_path_, 4);
    return server_fd_ >= 0;
}

int ControlServer::AcceptClientAndReadPid(int32_t* peer_pid)
{
    int client_fd = AcceptUnixClient(server_fd_);
    if (client_fd < 0) {
        return -1;
    }

    if (peer_pid == nullptr || !RecvBytes(client_fd, peer_pid, sizeof(*peer_pid))) {
        close(client_fd);
        return -1;
    }
    std::printf("client pid=%d connected\n", *peer_pid);
    std::fflush(stdout);
    return client_fd;
}

bool ControlServer::SendResources(int client_fd, const SharedResources& resources)
{
    if (client_fd < 0) {
        return false;
    }

    if (!SendBytes(client_fd, &resources.config, sizeof(resources.config))) {
        return false;
    }
    if (!SendFd(client_fd, resources.shm_fd)) {
        return false;
    }
    if (!SendFd(client_fd, resources.event_fd)) {
        return false;
    }
    return true;
}

}  // namespace linux_native_hook_v1
