#include <chrono>
#include <string>

#include "server/auth/registration_rate_limiter.h"
#include "tests/test_framework.h"

using Clock = std::chrono::steady_clock;

int testPerIpAndWindow() {
    chatbox::RegistrationRateLimiter limiter(20, 100, 3600);
    const auto t0 = Clock::time_point{};
    for (int i = 0; i < 20; ++i) {
        CHECK_TRUE(limiter.allow("203.0.113.7", t0));
    }
    CHECK_TRUE(!limiter.allow("203.0.113.7", t0));
    CHECK_TRUE(limiter.allow("203.0.113.8", t0));
    CHECK_TRUE(limiter.allow("203.0.113.7", t0 + std::chrono::seconds(3601)));
    return 0;
}

int testGlobalLimit() {
    chatbox::RegistrationRateLimiter limiter(20, 100, 3600);
    const auto t0 = Clock::time_point{};
    for (int i = 0; i < 100; ++i) {
        CHECK_TRUE(limiter.allow("198.51.100." + std::to_string(i + 1), t0));
    }
    CHECK_TRUE(!limiter.allow("198.51.100.200", t0));
    CHECK_TRUE(limiter.allow("198.51.100.200", t0 + std::chrono::seconds(3601)));
    return 0;
}

int main() {
    int failures = 0;
    failures += testPerIpAndWindow();
    failures += testGlobalLimit();
    if (failures == 0) {
        std::cout << "test_registration_rate_limiter: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
