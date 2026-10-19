#include "server/util/logging.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace chatbox {

namespace {

std::mutex g_log_mutex;
std::ofstream g_log_file;
bool g_use_file = false;
std::string g_log_path;
LogLevel g_min_level = LogLevel::kInfo;

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace

void initLogging(const std::string& file, LogLevel level) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_min_level = level;
    g_log_path = file;
    g_log_file.close();
    g_log_file.clear();
    g_use_file = false;
    if (!g_log_path.empty()) {
        g_log_file.open(g_log_path, std::ios::app);
        g_use_file = g_log_file.is_open();
    }
}

void reopenLogging() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_path.empty()) {
        return;
    }
    g_log_file.close();
    g_log_file.clear();
    g_log_file.open(g_log_path, std::ios::app);
    g_use_file = g_log_file.is_open();
}

void logMessage(LogLevel level, const std::string& module, const std::string& message) {
    if (static_cast<int>(level) < static_cast<int>(g_min_level)) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) %
                        1000;
    std::tm local_time{};
    localtime_r(&now_time, &local_time);
    char timestamp[32] = {0};
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);

    std::ostringstream line;
    line << timestamp << '.' << std::setw(3) << std::setfill('0') << millis.count() << " ["
         << std::this_thread::get_id() << "] " << levelName(level) << " " << module << " "
         << message;

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_use_file) {
        g_log_file << line.str() << '\n';
        g_log_file.flush();
    } else {
        std::ostream& out = (level >= LogLevel::kWarn) ? std::cerr : std::cout;
        out << line.str() << '\n';
        out.flush();
    }
}

bool parseLogLevel(const std::string& text, LogLevel& level) {
    if (text == "debug") {
        level = LogLevel::kDebug;
        return true;
    }
    if (text == "info") {
        level = LogLevel::kInfo;
        return true;
    }
    if (text == "warn") {
        level = LogLevel::kWarn;
        return true;
    }
    if (text == "error") {
        level = LogLevel::kError;
        return true;
    }
    return false;
}

}  // namespace chatbox
