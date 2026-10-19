#ifndef CHATROOM_SERVER_UTIL_LOGGING_H
#define CHATROOM_SERVER_UTIL_LOGGING_H

#include <string>

namespace chatroom {

enum class LogLevel {
    kDebug = 0,
    kInfo = 1,
    kWarn = 2,
    kError = 3,
};

void initLogging(const std::string& file, LogLevel level);
void reopenLogging();
void logMessage(LogLevel level, const std::string& module, const std::string& message);
bool parseLogLevel(const std::string& text, LogLevel& level);

}  // namespace chatroom

#endif  // CHATROOM_SERVER_UTIL_LOGGING_H
