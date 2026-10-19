#ifndef CHATBOX_CLIENT_CORE_MESSAGE_TIME_H
#define CHATBOX_CLIENT_CORE_MESSAGE_TIME_H

#include <QDateTime>
#include <QString>

namespace chatbox {
namespace client {

QDateTime parseServerUtc(const QString& text);
QString formatLocalTime(const QDateTime& utc);
QString formatLocalDate(const QDateTime& utc);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_MESSAGE_TIME_H
