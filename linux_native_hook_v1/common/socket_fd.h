#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace linux_native_hook_v1 {

int CreateUnixServerSocket(const std::string& socket_path, int backlog);
int AcceptUnixClient(int server_fd);
int ConnectUnixSocket(const std::string& socket_path);

bool SendFd(int socket_fd, int fd_to_send);
bool RecvFd(int socket_fd, int* out_fd);

bool SendBytes(int socket_fd, const void* buffer, size_t size);
bool RecvBytes(int socket_fd, void* buffer, size_t size);

}  // namespace linux_native_hook_v1
