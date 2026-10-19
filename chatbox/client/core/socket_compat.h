#ifndef CHATBOX_CLIENT_CORE_SOCKET_COMPAT_H
#define CHATBOX_CLIENT_CORE_SOCKET_COMPAT_H

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace chatbox {

bool initializeSockets(std::string& error);
void cleanupSockets();

bool connectWithTimeout(const std::string& host, uint16_t port, int timeout_ms,
                        SocketHandle& out_socket, std::string& error);
bool setSocketNonBlocking(SocketHandle socket, bool non_blocking);
void closeSocket(SocketHandle socket);
bool socketWouldBlock();
std::string lastSocketError();

}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_SOCKET_COMPAT_H
