#include "server/auth/registration_rate_limiter.h"

namespace chatbox {

RegistrationRateLimiter::RegistrationRateLimiter(int max_per_ip, int max_global,
                                                 int window_seconds)
    : max_per_ip_(max_per_ip), max_global_(max_global), window_(window_seconds) {}

bool RegistrationRateLimiter::allow(const std::string& ip, TimePoint now) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (now - global_.window_start > window_) {
        global_.count = 0;
        global_.window_start = now;
    }
    if (global_.count >= max_global_) {
        return false;
    }

    auto [it, inserted] = per_ip_.try_emplace(ip);
    if (inserted) {
        it->second.window_start = now;
    } else if (now - it->second.window_start > window_) {
        it->second.count = 0;
        it->second.window_start = now;
    }
    if (it->second.count >= max_per_ip_) {
        return false;
    }

    ++global_.count;
    ++it->second.count;

    if (per_ip_.size() > 4096) {
        eraseExpiredLocked(now);
    }
    return true;
}

void RegistrationRateLimiter::eraseExpiredLocked(TimePoint now) {
    for (auto it = per_ip_.begin(); it != per_ip_.end();) {
        if (now - it->second.window_start > window_) {
            it = per_ip_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace chatbox
