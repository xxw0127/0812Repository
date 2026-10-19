#include "server/util/metrics.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include "server/util/logging.h"

namespace chatroom {

namespace {

std::thread g_stats_thread;
std::mutex g_stats_mutex;
std::condition_variable g_stats_condition;
bool g_stats_running = false;

uint64_t readRssKb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) != 0) {
            continue;
        }
        std::istringstream stream(line.substr(6));
        uint64_t value = 0;
        stream >> value;
        return value;
    }
    return 0;
}

uint64_t countOpenFds() {
    std::error_code error;
    uint64_t count = 0;
    for (std::filesystem::directory_iterator it("/proc/self/fd", error), end;
         !error && it != end; it.increment(error)) {
        ++count;
    }
    return count;
}

}  // namespace

Metrics& metrics() {
    static Metrics instance;
    return instance;
}

MetricsSnapshot snapshotMetrics() {
    MetricsSnapshot snapshot;
    snapshot.connections_total = metrics().connections_total.load(std::memory_order_relaxed);
    snapshot.connections_active =
        metrics().connections_active.load(std::memory_order_relaxed);
    snapshot.auth_success = metrics().auth_success.load(std::memory_order_relaxed);
    snapshot.auth_failure = metrics().auth_failure.load(std::memory_order_relaxed);
    snapshot.messages_in = metrics().messages_in.load(std::memory_order_relaxed);
    snapshot.frames_out = metrics().frames_out.load(std::memory_order_relaxed);
    snapshot.protocol_errors = metrics().protocol_errors.load(std::memory_order_relaxed);
    snapshot.rate_limited = metrics().rate_limited.load(std::memory_order_relaxed);
    snapshot.db_errors = metrics().db_errors.load(std::memory_order_relaxed);
    snapshot.broadcast_dropped =
        metrics().broadcast_dropped.load(std::memory_order_relaxed);
    snapshot.rss_kb = readRssKb();
    snapshot.open_fds = countOpenFds();
    return snapshot;
}

std::string formatMetrics(const MetricsSnapshot& snapshot) {
    std::ostringstream line;
    line << "conn_total=" << snapshot.connections_total
         << " conn_active=" << snapshot.connections_active
         << " auth_ok=" << snapshot.auth_success
         << " auth_fail=" << snapshot.auth_failure
         << " msg_in=" << snapshot.messages_in
         << " frames_out=" << snapshot.frames_out
         << " protocol_errors=" << snapshot.protocol_errors
         << " rate_limited=" << snapshot.rate_limited
         << " db_errors=" << snapshot.db_errors
         << " bcast_drop=" << snapshot.broadcast_dropped
         << " rss_kb=" << snapshot.rss_kb
         << " fds=" << snapshot.open_fds;
    return line.str();
}

void startStatsReporter() {
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    if (g_stats_running) {
        return;
    }
    g_stats_running = true;
    g_stats_thread = std::thread([] {
        logMessage(LogLevel::kInfo, "stats", formatMetrics(snapshotMetrics()));
        std::unique_lock<std::mutex> lock(g_stats_mutex);
        while (g_stats_running) {
            if (g_stats_condition.wait_for(lock, std::chrono::seconds(10), [] {
                    return !g_stats_running;
                })) {
                break;
            }
            lock.unlock();
            logMessage(LogLevel::kInfo, "stats", formatMetrics(snapshotMetrics()));
            lock.lock();
        }
    });
}

void stopStatsReporter() {
    {
        std::lock_guard<std::mutex> lock(g_stats_mutex);
        if (!g_stats_running) {
            return;
        }
        g_stats_running = false;
    }
    g_stats_condition.notify_all();
    if (g_stats_thread.joinable()) {
        g_stats_thread.join();
    }
}

}  // namespace chatroom
