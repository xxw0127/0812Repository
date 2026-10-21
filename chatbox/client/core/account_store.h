#ifndef CHATBOX_CLIENT_CORE_ACCOUNT_STORE_H
#define CHATBOX_CLIENT_CORE_ACCOUNT_STORE_H

#include <QString>
#include <QVector>

namespace chatbox::client {

// Presentation data for the account switcher. The password lives in
// credential_store (DPAPI); this only keeps what the list needs to render,
// so an account can be shown with its real avatar and nickname before the
// user is authenticated again.
struct AccountRecord {
    QString username;
    QString nickname;
    quint64 user_id = 0;
    QString avatar_path;
};

QVector<AccountRecord> loadAccounts();
void upsertAccount(const AccountRecord& record);
void forgetAccountRecord(const QString& username);

// Where the cached avatar PNG for an account lives. Returns an empty string
// when no writable app-data directory is available.
QString accountAvatarPath(const QString& username);

}  // namespace chatbox::client

#endif  // CHATBOX_CLIENT_CORE_ACCOUNT_STORE_H
