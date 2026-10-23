#ifndef CHATBOX_CLIENT_CORE_CREDENTIAL_STORE_H
#define CHATBOX_CLIENT_CORE_CREDENTIAL_STORE_H

#include <QString>
#include <QStringList>

namespace chatbox::client {

// Password storage is OS-backed on Windows (DPAPI). Other builds must not
// fall back to plaintext QSettings.
bool passwordStorageSupported();
bool saveSavedCredentials(const QString& username, const QString& password);
bool loadSavedCredentials(QString* username, QString* password);
// Only clears the single "remembered" pair shown on the login page.
void clearSavedCredentials();

// Per-account credentials, keyed by username. Every successful "remember
// password" login also lands here, so the switcher starts populated without
// the user having to do anything extra.
bool saveAccountCredentials(const QString& username, const QString& password);
bool loadAccountCredentials(const QString& username, QString* password);
// Removes only the password secret; the account's display record and cached
// avatar remain so it can still be selected and prompted for a password.
void forgetAccountCredentials(const QString& username);
QStringList savedAccounts();
void forgetAccount(const QString& username);

}  // namespace chatbox::client

#endif  // CHATBOX_CLIENT_CORE_CREDENTIAL_STORE_H
