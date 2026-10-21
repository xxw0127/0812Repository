#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "server/net/connection.h"
#include "tests/test_framework.h"

namespace {

std::vector<unsigned char> makeProxyV2Header(
    unsigned char command, unsigned char family,
    const std::vector<unsigned char>& address) {
    std::vector<unsigned char> header = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D,
                                         0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A};
    header.push_back(static_cast<unsigned char>(0x20 | command));
    header.push_back(family);
    header.push_back(static_cast<unsigned char>((address.size() >> 8) & 0xFF));
    header.push_back(static_cast<unsigned char>(address.size() & 0xFF));
    header.insert(header.end(), address.begin(), address.end());
    return header;
}

int testPartialV2Tcp4Header() {
    int fds[2] = {-1, -1};
    CHECK_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    const std::vector<unsigned char> address = {203, 0, 113, 7, 192, 0, 2, 1,
                                                0x30, 0x39, 0x1F, 0x90};
    const std::vector<unsigned char> header = makeProxyV2Header(0x01, 0x11, address);
    CHECK_EQ(header.size(), static_cast<std::size_t>(28));

    {
        auto connection = std::make_shared<chatbox::Connection>(
            fds[0], 1, 0, "127.0.0.1", true);
        std::string error;
        connection->input().append(reinterpret_cast<const char*>(header.data()), 10);
        CHECK_TRUE(connection->tryParseProxyHeader(error));
        CHECK_TRUE(connection->proxyHeaderPending());
        CHECK_EQ(connection->peerIp(), std::string("127.0.0.1"));

        connection->input().append(
            reinterpret_cast<const char*>(header.data() + 10), header.size() - 10);
        CHECK_TRUE(connection->tryParseProxyHeader(error));
        CHECK_TRUE(!connection->proxyHeaderPending());
        CHECK_EQ(connection->peerIp(), std::string("203.0.113.7"));
    }
    ::close(fds[1]);
    return 0;
}

int testLocalHeaderKeepsInitialIp() {
    int fds[2] = {-1, -1};
    CHECK_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    const std::vector<unsigned char> header = makeProxyV2Header(0x00, 0x00, {});
    {
        auto connection = std::make_shared<chatbox::Connection>(
            fds[0], 2, 0, "127.0.0.1", true);
        std::string error;
        connection->input().append(reinterpret_cast<const char*>(header.data()),
                                   header.size());
        CHECK_TRUE(connection->tryParseProxyHeader(error));
        CHECK_TRUE(!connection->proxyHeaderPending());
        CHECK_EQ(connection->peerIp(), std::string("127.0.0.1"));
    }
    ::close(fds[1]);
    return 0;
}

int testInvalidTextHeader() {
    int fds[2] = {-1, -1};
    CHECK_TRUE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    {
        auto connection = std::make_shared<chatbox::Connection>(
            fds[0], 3, 0, "127.0.0.1", true);
        const std::string invalid = "BOGUS HEADER\r\n";
        connection->input().append(invalid.data(), invalid.size());
        std::string error;
        CHECK_TRUE(!connection->tryParseProxyHeader(error));
        CHECK_TRUE(!error.empty());
    }
    ::close(fds[1]);
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testPartialV2Tcp4Header();
    failures += testLocalHeaderKeepsInitialIp();
    failures += testInvalidTextHeader();
    if (failures == 0) {
        std::cout << "test_connection_proxy: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
