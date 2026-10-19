#include "core/socket_compat.h"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <cstdio>
#else
#include <fcntl.h>
#include <netinet/tcp.h>
#endif

namespace chatroom {

#ifdef _WIN32

bool initializeSockets(std::string& error) {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        error = "WSAStartup failed: " + std::to_string(result);
        return false;
    }
    error.clear();
    return true;
}

void cleanupSockets() {
    WSACleanup();
}

std::string lastSocketError() {
    return "socket error " + std::to_string(WSAGetLastError());
}

bool socketWouldBlock() {
    const int code = WSAGetLastError();
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
}

namespace {

bool setNonBlocking(SocketHandle socket, bool non_blocking) {
    u_long mode = non_blocking ? 1 : 0;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
}

}  // namespace

#else

bool initializeSockets(std::string& error) {
    error.clear();
    return true;
}

void cleanupSockets() {}

std::string lastSocketError() {
    return std::strerror(errno);
}

bool socketWouldBlock() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
}

namespace {

bool setNonBlocking(SocketHandle socket, bool non_blocking) {
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int updated = non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(socket, F_SETFL, updated) == 0;
}

}  // namespace

#endif

bool setSocketNonBlocking(SocketHandle socket, bool non_blocking) {
    return setNonBlocking(socket, non_blocking);
}

bool connectWithTimeout(const std::string& host, uint16_t port, int timeout_ms,
                        SocketHandle& out_socket, std::string& error) {
    out_socket = kInvalidSocket;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses = nullptr;
    const std::string port_text = std::to_string(port);
    const int resolve_result = getaddrinfo(host.c_str(), port_text.c_str(), &hints, &addresses);
    if (resolve_result != 0) {
#ifdef _WIN32
        error = std::string("getaddrinfo failed: ") + gai_strerrorA(resolve_result);
#else
        error = std::string("getaddrinfo failed: ") + gai_strerror(resolve_result);
#endif
        return false;
    }

    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
#ifdef _WIN32
        SocketHandle socket = ::socket(address->ai_family, address->ai_socktype,
                                       address->ai_protocol);
#else
        SocketHandle socket = ::socket(address->ai_family, address->ai_socktype,
                                       address->ai_protocol);
#endif
        if (socket == kInvalidSocket) {
            continue;
        }
        if (!setNonBlocking(socket, true)) {
            closeSocket(socket);
            continue;
        }

        int connect_result = ::connect(socket, address->ai_addr,
                                       static_cast<int>(address->ai_addrlen));
        if (connect_result != 0) {
            if (!socketWouldBlock()) {
                closeSocket(socket);
                continue;
            }

            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(socket, &write_set);
            timeval timeout{};
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
            const int selected = ::select(0, nullptr, &write_set, nullptr, &timeout);
#else
            const int selected = ::select(socket + 1, nullptr, &write_set, nullptr, &timeout);
#endif
            if (selected <= 0) {
                error = selected == 0 ? "connection timed out" : lastSocketError();
                closeSocket(socket);
                continue;
            }

            int socket_error = 0;
#ifdef _WIN32
            int length = sizeof(socket_error);
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&socket_error), &length) != 0) {
#else
            socklen_t length = sizeof(socket_error);
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &socket_error, &length) != 0) {
#endif
                error = lastSocketError();
                closeSocket(socket);
                continue;
            }
            if (socket_error != 0) {
                error = "connect failed: " + std::to_string(socket_error);
                closeSocket(socket);
                continue;
            }
        }

        setNonBlocking(socket, false);
        int no_delay = 1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));

        out_socket = socket;
        error.clear();
        freeaddrinfo(addresses);
        return true;
    }

    freeaddrinfo(addresses);
    if (error.empty()) {
        error = "no reachable address";
    }
    return false;
}

void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    ::shutdown(socket, SD_BOTH);
    ::closesocket(socket);
#else
    ::shutdown(socket, SHUT_RDWR);
    ::close(socket);
#endif
}

}  // namespace chatroom
