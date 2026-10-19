#ifndef CHATBOX_SERVER_UTIL_TOKEN_BUCKET_H
#define CHATBOX_SERVER_UTIL_TOKEN_BUCKET_H

#include <algorithm>
#include <chrono>

namespace chatbox {

class TokenBucket {
public:
    TokenBucket(double rate_per_second, double burst)
        : rate_per_second_(rate_per_second),
          burst_(burst),
          tokens_(burst),
          last_refill_(std::chrono::steady_clock::now()) {}

    bool consume(double tokens = 1.0) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed =
            std::chrono::duration<double>(now - last_refill_).count();
        last_refill_ = now;
        tokens_ = std::min(burst_, tokens_ + elapsed * rate_per_second_);
        if (tokens_ < tokens) {
            return false;
        }
        tokens_ -= tokens;
        return true;
    }

private:
    double rate_per_second_;
    double burst_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_UTIL_TOKEN_BUCKET_H
