#ifndef CHATBOX_CLIENT_CORE_CREDENTIAL_STORE_H
#define CHATBOX_CLIENT_CORE_CREDENTIAL_STORE_H

#include <QString>

namespace chatbox::client {

// Password storage is OS-backed on Windows (DPAPI). Other builds must not
// fall back to plaintext QSettings.
bool passwordStorageSupported();
bool saveSavedCredentials(const QString& username, const QString& password);
bool loadSavedCredentials(QString* username, QString* password);
void clearSavedCredentials();

}  // namespace chatbox::client

#endif  // CHATBOX_CLIENT_CORE_CREDENTIAL_STORE_H
