#include <string>

#include "server/net/connection_limiter.h"
#include "tests/test_framework.h"

int testPerIpRelease() {
    chatbox::ConnectionLimiter limiter(2, 1);
    std::string error;
    CHECK_TRUE(limiter.acquire(1, "a", error));
    CHECK_TRUE(!limiter.acquire(2, "a", error));
    CHECK_TRUE(limiter.acquire(3, "b", error));
    limiter.release(1);
    CHECK_TRUE(limiter.acquire(4, "a", error));
    limiter.release(2);
    limiter.release(3);
    limiter.release(4);
    return 0;
}

int testDeferredRekey() {
    chatbox::ConnectionLimiter limiter(10, 1);
    std::string error;
    CHECK_TRUE(limiter.acquire(1, "127.0.0.1", error, true));
    CHECK_TRUE(limiter.acquire(2, "127.0.0.1", error, true));
    CHECK_TRUE(limiter.rekey(1, "203.0.113.7", error));
    CHECK_TRUE(!limiter.rekey(2, "203.0.113.7", error));
    limiter.release(1);
    CHECK_TRUE(limiter.rekey(2, "203.0.113.7", error));
    limiter.release(2);
    return 0;
}

int testFiftyPerIp() {
    chatbox::ConnectionLimiter limiter(100, 50);
    std::string error;
    for (int i = 1; i <= 50; ++i) {
        CHECK_TRUE(limiter.acquire(static_cast<uint64_t>(i), "192.0.2.50", error));
    }
    CHECK_TRUE(!limiter.acquire(51, "192.0.2.50", error));
    limiter.release(1);
    CHECK_TRUE(limiter.acquire(52, "192.0.2.50", error));
    for (int i = 2; i <= 50; ++i) {
        limiter.release(static_cast<uint64_t>(i));
    }
    limiter.release(52);
    return 0;
}

int main() {
    int failures = 0;
    failures += testPerIpRelease();
    failures += testDeferredRekey();
    failures += testFiftyPerIp();
    if (failures == 0) {
        std::cout << "test_connection_limiter: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
