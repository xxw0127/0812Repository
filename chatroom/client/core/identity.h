#ifndef CHATROOM_CLIENT_CORE_IDENTITY_H
#define CHATROOM_CLIENT_CORE_IDENTITY_H

#include <QString>
#include <QtGlobal>

namespace chatroom {
namespace client {

inline QString publicUserId(quint64 user_id) {
    return QString("U%1").arg(user_id, 8, 10, QChar('0'));
}

}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_IDENTITY_H
