#ifndef CHATBOX_SERVER_NET_CONNECTION_LIMITER_H
#define CHATBOX_SERVER_NET_CONNECTION_LIMITER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chatbox {

class ConnectionLimiter {
public:
    ConnectionLimiter(int max_connections, int max_per_ip);

    bool acquire(uint64_t connection_id, const std::string& ip, std::string& error,
                 bool defer_per_ip = false);
    bool rekey(uint64_t connection_id, const std::string& ip, std::string& error);
    void release(uint64_t connection_id);

private:
    struct TrackedConnection {
        std::string ip;
        bool counted = false;
    };

    int max_connections_;
    int max_per_ip_;
    std::atomic<int> active_{0};
    std::mutex mutex_;
    std::unordered_map<std::string, int> ip_counts_;
    std::unordered_map<uint64_t, TrackedConnection> connections_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_NET_CONNECTION_LIMITER_H
