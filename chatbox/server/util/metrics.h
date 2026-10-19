#ifndef CHATBOX_SERVER_UTIL_METRICS_H
#define CHATBOX_SERVER_UTIL_METRICS_H

#include <atomic>
#include <cstdint>
#include <string>

namespace chatbox {

struct Metrics {
    std::atomic<uint64_t> connections_total{0};
    std::atomic<uint64_t> connections_active{0};
    std::atomic<uint64_t> auth_success{0};
    std::atomic<uint64_t> auth_failure{0};
    std::atomic<uint64_t> messages_in{0};
    std::atomic<uint64_t> frames_out{0};
    std::atomic<uint64_t> protocol_errors{0};
    std::atomic<uint64_t> rate_limited{0};
    std::atomic<uint64_t> db_errors{0};
    std::atomic<uint64_t> broadcast_dropped{0};
};

struct MetricsSnapshot {
    uint64_t connections_total = 0;
    uint64_t connections_active = 0;
    uint64_t auth_success = 0;
    uint64_t auth_failure = 0;
    uint64_t messages_in = 0;
    uint64_t frames_out = 0;
    uint64_t protocol_errors = 0;
    uint64_t rate_limited = 0;
    uint64_t db_errors = 0;
    uint64_t broadcast_dropped = 0;
    uint64_t rss_kb = 0;
    uint64_t open_fds = 0;
};

Metrics& metrics();
MetricsSnapshot snapshotMetrics();
std::string formatMetrics(const MetricsSnapshot& snapshot);

void startStatsReporter();
void stopStatsReporter();

}  // namespace chatbox

#endif  // CHATBOX_SERVER_UTIL_METRICS_H
