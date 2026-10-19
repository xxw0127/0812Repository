#ifndef CHATROOM_CLIENT_CORE_FRIEND_VIEW_H
#define CHATROOM_CLIENT_CORE_FRIEND_VIEW_H

#include <QString>
#include <QtGlobal>

namespace chatroom {
namespace client {

struct FriendView {
    quint64 user_id = 0;
    QString username;
    QString nickname;
    QString remark;
    quint32 avatar_version = 0;
    bool online = false;
    int unread = 0;
    QString last_content;
    QString last_time;
};

}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_FRIEND_VIEW_H
