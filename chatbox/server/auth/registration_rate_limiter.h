#ifndef CHATBOX_SERVER_AUTH_REGISTRATION_RATE_LIMITER_H
#define CHATBOX_SERVER_AUTH_REGISTRATION_RATE_LIMITER_H

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chatbox {

class RegistrationRateLimiter {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    RegistrationRateLimiter(int max_per_ip, int max_global, int window_seconds);
    bool allow(const std::string& ip, TimePoint now);

private:
    struct Entry {
        int count = 0;
        TimePoint window_start;
    };

    void eraseExpiredLocked(TimePoint now);

    int max_per_ip_;
    int max_global_;
    std::chrono::seconds window_;
    mutable std::mutex mutex_;
    Entry global_;
    std::unordered_map<std::string, Entry> per_ip_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_REGISTRATION_RATE_LIMITER_H
