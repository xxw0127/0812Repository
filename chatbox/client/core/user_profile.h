#ifndef CHATROOM_CLIENT_CORE_USER_PROFILE_H
#define CHATROOM_CLIENT_CORE_USER_PROFILE_H

#include <QImage>
#include <QString>
#include <QtGlobal>

namespace chatroom {
namespace client {

// Public profile data returned by PROFILE_GET. relationship follows the
// protocol values: 0 self, 1 friend, 2 none, 3 request sent, 4 request
// received.
struct UserProfile {
    quint64 user_id = 0;
    QString username;
    QString nickname;
    QString signature;
    QString remark;
    quint32 avatar_version = 0;
    QImage avatar;
    bool online = false;
    quint8 relationship = 2;
};

// Remark is viewer-local and wins over the server nickname. Whitespace-only
// remarks count as empty. No other fallback is applied here so callers can
// distinguish an empty nickname.
inline QString profileDisplayName(const UserProfile& profile) {
    const QString remark = profile.remark.trimmed();
    return remark.isEmpty() ? profile.nickname : remark;
}

}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_USER_PROFILE_H
