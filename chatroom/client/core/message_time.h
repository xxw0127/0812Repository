#ifndef CHATROOM_CLIENT_CORE_MESSAGE_TIME_H
#define CHATROOM_CLIENT_CORE_MESSAGE_TIME_H

#include <QDateTime>
#include <QString>

namespace chatroom {
namespace client {

QDateTime parseServerUtc(const QString& text);
QString formatLocalTime(const QDateTime& utc);
QString formatLocalDate(const QDateTime& utc);

}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_MESSAGE_TIME_H
