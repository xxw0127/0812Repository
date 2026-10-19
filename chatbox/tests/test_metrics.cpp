#include <cstdint>
#include <iostream>
#include <string>

#include "server/util/metrics.h"

int main() {
    const chatroom::MetricsSnapshot before = chatroom::snapshotMetrics();
    chatroom::metrics().messages_in.fetch_add(3, std::memory_order_relaxed);
    chatroom::metrics().connections_active.fetch_add(1, std::memory_order_relaxed);
    const chatroom::MetricsSnapshot after = chatroom::snapshotMetrics();

    if (after.messages_in != before.messages_in + 3) {
        std::cerr << "messages_in counter mismatch\n";
        return 1;
    }
    if (after.connections_active != before.connections_active + 1) {
        std::cerr << "connections_active counter mismatch\n";
        return 1;
    }
    const std::string line = chatroom::formatMetrics(after);
    if (line.find("msg_in=") == std::string::npos ||
        line.find("conn_active=") == std::string::npos ||
        line.find("rss_kb=") == std::string::npos) {
        std::cerr << "formatted metrics missing fields: " << line << '\n';
        return 1;
    }

    chatroom::metrics().connections_active.fetch_sub(1, std::memory_order_relaxed);
    std::cout << "metrics checks passed\n";
    return 0;
}
