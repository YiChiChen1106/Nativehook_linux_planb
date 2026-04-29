#include "common/socket_fd.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace linux_native_hook_v1 {

namespace {

sockaddr_un BuildUnixAddress(const std::string& socket_path)
{
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path.c_str());
    return addr;
}

}  // namespace

int CreateUnixServerSocket(const std::string& socket_path, int backlog)
{
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        return -1;
    }

    sockaddr_un addr = BuildUnixAddress(socket_path);
    unlink(socket_path.c_str());
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(server_fd);
        return -1;
    }
    if (listen(server_fd, backlog) != 0) {
        close(server_fd);
        return -1;
    }
    return server_fd;
}

int AcceptUnixClient(int server_fd)
{
    return accept(server_fd, nullptr, nullptr);
}

int ConnectUnixSocket(const std::string& socket_path)
{
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }

    sockaddr_un addr = BuildUnixAddress(socket_path);
    if (connect(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

bool SendFd(int socket_fd, int fd_to_send)
{
    char data = 0;
    iovec iov {};
    iov.iov_base = &data;
    iov.iov_len = sizeof(data);

    char control[CMSG_SPACE(sizeof(int))];
    std::memset(control, 0, sizeof(control));

    msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(fd_to_send));
    msg.msg_controllen = CMSG_SPACE(sizeof(int));

    return sendmsg(socket_fd, &msg, 0) >= 0;
}

bool RecvFd(int socket_fd, int* out_fd)
{
    if (out_fd == nullptr) {
        return false;
    }

    char data = 0;
    iovec iov {};
    iov.iov_base = &data;
    iov.iov_len = sizeof(data);

    char control[CMSG_SPACE(sizeof(int))];
    std::memset(control, 0, sizeof(control));

    msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    if (recvmsg(socket_fd, &msg, 0) <= 0) {
        return false;
    }

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
        return false;
    }

    std::memcpy(out_fd, CMSG_DATA(cmsg), sizeof(*out_fd));
    return true;
}

bool SendBytes(int socket_fd, const void* buffer, size_t size)
{
    const uint8_t* cursor = static_cast<const uint8_t*>(buffer);
    size_t sent = 0;
    while (sent < size) {
        ssize_t ret = send(socket_fd, cursor + sent, size - sent, 0);
        if (ret <= 0) {
            return false;
        }
        sent += static_cast<size_t>(ret);
    }
    return true;
}

bool RecvBytes(int socket_fd, void* buffer, size_t size)
{
    uint8_t* cursor = static_cast<uint8_t*>(buffer);
    size_t received = 0;
    while (received < size) {
        ssize_t ret = recv(socket_fd, cursor + received, size - received, 0);
        if (ret <= 0) {
            return false;
        }
        received += static_cast<size_t>(ret);
    }
    return true;
}

}  // namespace linux_native_hook_v1
