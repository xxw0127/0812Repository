#include "server/net/connection_limiter.h"

#include "server/util/metrics.h"

namespace chatroom {

ConnectionLimiter::ConnectionLimiter(int max_connections, int max_per_ip)
    : max_connections_(max_connections), max_per_ip_(max_per_ip) {}

bool ConnectionLimiter::acquire(uint64_t connection_id, const std::string& ip,
                                std::string& error) {
    const int active = active_.fetch_add(1) + 1;
    if (max_connections_ > 0 && active > max_connections_) {
        active_.fetch_sub(1);
        error = "server connection limit reached";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (max_per_ip_ > 0) {
        auto ip_it = ip_counts_.find(ip);
        const int count = ip_it == ip_counts_.end() ? 0 : ip_it->second;
        if (count >= max_per_ip_) {
            error = "per-ip connection limit reached";
            active_.fetch_sub(1);
            return false;
        }
        ++ip_counts_[ip];
    }
    connection_ips_[connection_id] = ip;
    metrics().connections_total.fetch_add(1, std::memory_order_relaxed);
    metrics().connections_active.fetch_add(1, std::memory_order_relaxed);
    error.clear();
    return true;
}

void ConnectionLimiter::release(uint64_t connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connection_ips_.find(connection_id);
    if (it == connection_ips_.end()) {
        return;
    }
    const std::string ip = it->second;
    connection_ips_.erase(it);
    if (max_per_ip_ > 0) {
        auto ip_it = ip_counts_.find(ip);
        if (ip_it != ip_counts_.end()) {
            if (--ip_it->second <= 0) {
                ip_counts_.erase(ip_it);
            }
        }
    }
    active_.fetch_sub(1);
    metrics().connections_active.fetch_sub(1, std::memory_order_relaxed);
}

}  // namespace chatroom
