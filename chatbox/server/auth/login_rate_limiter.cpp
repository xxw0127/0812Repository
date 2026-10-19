#include "server/auth/login_rate_limiter.h"

namespace chatbox {

LoginRateLimiter::LoginRateLimiter(int max_failures, int window_seconds)
    : max_failures_(max_failures), window_(window_seconds) {}

std::string LoginRateLimiter::key(const std::string& ip,
                                  const std::string& username) const {
    return ip + "\n" + username;
}

bool LoginRateLimiter::isBlocked(const std::string& ip,
                                 const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    auto it = entries_.find(key(ip, username));
    if (it == entries_.end()) {
        return false;
    }
    if (now - it->second.window_start > window_) {
        entries_.erase(it);
        return false;
    }
    return it->second.failures >= max_failures_;
}

void LoginRateLimiter::recordFailure(const std::string& ip,
                                     const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (entries_.size() > 4096) {
        eraseExpiredLocked(now);
    }
    auto [it, inserted] = entries_.try_emplace(key(ip, username));
    if (inserted || now - it->second.window_start > window_) {
        it->second.failures = 1;
        it->second.window_start = now;
        return;
    }
    ++it->second.failures;
}

void LoginRateLimiter::recordSuccess(const std::string& ip,
                                     const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(key(ip, username));
}

void LoginRateLimiter::eraseExpiredLocked(
    std::chrono::steady_clock::time_point now) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now - it->second.window_start > window_) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace chatbox
