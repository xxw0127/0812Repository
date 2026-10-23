#include "core/credential_store.h"

#include <QByteArray>
#include <QSettings>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace chatbox::client {

#ifdef Q_OS_WIN
namespace {

QByteArray protectPassword(const QByteArray& plain) {
    if (plain.isEmpty()) {
        return {};
    }
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    input.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"ChatBox saved password", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    const QByteArray encoded(reinterpret_cast<const char*>(output.pbData),
                             static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return encoded;
}

bool unprotectPassword(const QByteArray& encoded, QByteArray* plain) {
    if (encoded.isEmpty() || plain == nullptr) {
        return false;
    }
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encoded.constData()));
    input.cbData = static_cast<DWORD>(encoded.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }
    *plain = QByteArray(reinterpret_cast<const char*>(output.pbData),
                        static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return true;
}

}  // namespace
#endif

bool passwordStorageSupported() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool saveSavedCredentials(const QString& username, const QString& password) {
#ifdef Q_OS_WIN
    const QByteArray encrypted = protectPassword(password.toUtf8());
    if (encrypted.isEmpty()) {
        return false;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("login/username"), username);
    settings.setValue(QStringLiteral("login/password"),
                      QString::fromLatin1(encrypted.toBase64()));
    settings.sync();
    // Keep the switcher's per-account copy in step; otherwise an account the
    // user just saved would be missing from the list.
    saveAccountCredentials(username, password);
    return true;
#else
    Q_UNUSED(username);
    Q_UNUSED(password);
    return false;
#endif
}

bool loadSavedCredentials(QString* username, QString* password) {
    if (username == nullptr || password == nullptr) {
        return false;
    }
#ifdef Q_OS_WIN
    QSettings settings;
    *username = settings.value(QStringLiteral("login/username")).toString();
    const QByteArray encrypted = QByteArray::fromBase64(
        settings.value(QStringLiteral("login/password")).toString().toLatin1());
    QByteArray plain;
    if (!unprotectPassword(encrypted, &plain)) {
        return false;
    }
    *password = QString::fromUtf8(plain);
    return true;
#else
    Q_UNUSED(username);
    Q_UNUSED(password);
    return false;
#endif
}

void clearSavedCredentials() {
    QSettings settings;
    settings.remove(QStringLiteral("login/username"));
    settings.remove(QStringLiteral("login/password"));
    settings.sync();
}

namespace {

QString accountSecretKey(const QString& username) {
    return QStringLiteral("accounts/%1/secret").arg(username);
}

}  // namespace

bool saveAccountCredentials(const QString& username, const QString& password) {
#ifdef Q_OS_WIN
    const QString trimmed = username.trimmed();
    if (trimmed.isEmpty() || password.isEmpty()) {
        return false;
    }
    const QByteArray encrypted = protectPassword(password.toUtf8());
    if (encrypted.isEmpty()) {
        return false;
    }
    QSettings settings;
    settings.setValue(accountSecretKey(trimmed),
                      QString::fromLatin1(encrypted.toBase64()));
    settings.sync();
    return true;
#else
    Q_UNUSED(username);
    Q_UNUSED(password);
    return false;
#endif
}

bool loadAccountCredentials(const QString& username, QString* password) {
    if (password == nullptr) {
        return false;
    }
#ifdef Q_OS_WIN
    QSettings settings;
    const QByteArray encrypted = QByteArray::fromBase64(
        settings.value(accountSecretKey(username.trimmed())).toString().toLatin1());
    QByteArray plain;
    if (!unprotectPassword(encrypted, &plain)) {
        // Accounts remembered before the switcher existed only live under
        // login/*. Falling back here is what keeps "记住密码" actually meaning
        // "no password prompt" when switching back to that account.
        QString legacy_user;
        QString legacy_password;
        if (loadSavedCredentials(&legacy_user, &legacy_password) &&
            legacy_user.compare(username.trimmed(), Qt::CaseInsensitive) == 0) {
            *password = legacy_password;
            return true;
        }
        return false;
    }
    *password = QString::fromUtf8(plain);
    return true;
#else
    Q_UNUSED(username);
    return false;
#endif
}

QStringList savedAccounts() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("accounts"));
    QStringList names = settings.childGroups();
    settings.endGroup();
    names.sort(Qt::CaseInsensitive);
    return names;
}

void forgetAccountCredentials(const QString& username) {
    const QString trimmed = username.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.remove(QStringLiteral("accounts/%1/secret").arg(trimmed));
    settings.sync();
}

void forgetAccount(const QString& username) {
    QSettings settings;
    settings.remove(QStringLiteral("accounts/%1").arg(username.trimmed()));
    settings.sync();
}

}  // namespace chatbox::client
