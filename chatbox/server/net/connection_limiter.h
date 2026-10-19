#ifndef CHATROOM_SERVER_NET_CONNECTION_LIMITER_H
#define CHATROOM_SERVER_NET_CONNECTION_LIMITER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chatroom {

class ConnectionLimiter {
public:
    ConnectionLimiter(int max_connections, int max_per_ip);

    bool acquire(uint64_t connection_id, const std::string& ip, std::string& error);
    void release(uint64_t connection_id);

private:
    int max_connections_;
    int max_per_ip_;
    std::atomic<int> active_{0};
    std::mutex mutex_;
    std::unordered_map<std::string, int> ip_counts_;
    std::unordered_map<uint64_t, std::string> connection_ips_;
};

}  // namespace chatroom

#endif  // CHATROOM_SERVER_NET_CONNECTION_LIMITER_H
