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

}  // namespace chatbox::client
