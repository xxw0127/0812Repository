#ifndef CHATBOX_SERVER_AUTH_LOGIN_RATE_LIMITER_H
#define CHATBOX_SERVER_AUTH_LOGIN_RATE_LIMITER_H

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chatbox {

class LoginRateLimiter {
public:
    LoginRateLimiter(int max_failures, int window_seconds);

    bool isBlocked(const std::string& ip, const std::string& username) const;
    void recordFailure(const std::string& ip, const std::string& username);
    void recordSuccess(const std::string& ip, const std::string& username);

private:
    struct Entry {
        int failures = 0;
        std::chrono::steady_clock::time_point window_start;
    };

    std::string key(const std::string& ip, const std::string& username) const;
    void eraseExpiredLocked(std::chrono::steady_clock::time_point now);

    int max_failures_;
    std::chrono::seconds window_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, Entry> entries_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_LOGIN_RATE_LIMITER_H
