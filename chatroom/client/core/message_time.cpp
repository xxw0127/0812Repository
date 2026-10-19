#include "core/message_time.h"

namespace chatroom {
namespace client {

QDateTime parseServerUtc(const QString& text) {
    if (text.size() != 19 || text.at(4) != '-' || text.at(7) != '-' ||
        text.at(10) != ' ' || text.at(13) != ':' || text.at(16) != ':') {
        return {};
    }
    const QDate date = QDate::fromString(text.mid(0, 10), "yyyy-MM-dd");
    const QTime time = QTime::fromString(text.mid(11, 8), "HH:mm:ss");
    if (!date.isValid() || !time.isValid()) {
        return {};
    }
    return QDateTime(date, time, Qt::UTC);
}

QString formatLocalTime(const QDateTime& utc) {
    if (!utc.isValid()) {
        return "--:--";
    }
    return utc.toLocalTime().toString("HH:mm");
}

QString formatLocalDate(const QDateTime& utc) {
    if (!utc.isValid()) {
        return "--";
    }
    return utc.toLocalTime().toString("yyyy-MM-dd");
}

}  // namespace client
}  // namespace chatroom
