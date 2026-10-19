#ifndef CHATBOX_CLIENT_CORE_PROTOCOL_QT_H
#define CHATBOX_CLIENT_CORE_PROTOCOL_QT_H

#include <QByteArray>
#include <QString>

#include "common/protocol.h"

namespace chatbox {

QByteArray encodeToByteArray(const Frame& frame);
bool decodeFromByteArray(const QByteArray& data, Frame& frame);
QString fromUtf8(const std::string& value);
std::string toUtf8(const QString& value);

}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_PROTOCOL_QT_H
