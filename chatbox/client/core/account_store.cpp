#include "core/account_store.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace chatbox::client {

namespace {

QString accountsRoot() {
    const QString data = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    if (data.isEmpty()) {
        return QString();
    }
    return data + QStringLiteral("/accounts");
}

QString sanitized(const QString& username) {
    QString safe = username;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),
                 QStringLiteral("_"));
    return safe;
}

}  // namespace

QString accountAvatarPath(const QString& username) {
    const QString root = accountsRoot();
    if (root.isEmpty() || username.trimmed().isEmpty()) {
        return QString();
    }
    QDir().mkpath(root);
    return root + QStringLiteral("/") + sanitized(username.trimmed()) +
           QStringLiteral(".png");
}

QVector<AccountRecord> loadAccounts() {
    QVector<AccountRecord> records;
    QSettings settings;
    settings.beginGroup(QStringLiteral("accounts"));
    const QStringList names = settings.childGroups();
    for (const QString& name : names) {
        settings.beginGroup(name);
        AccountRecord record;
        record.username = name;
        record.nickname = settings.value(QStringLiteral("nickname")).toString();
        record.user_id = settings.value(QStringLiteral("user_id")).toULongLong();
        record.avatar_path = accountAvatarPath(name);
        if (record.avatar_path.isEmpty() ||
            !QFile::exists(record.avatar_path)) {
            record.avatar_path.clear();
        }
        settings.endGroup();
        records.append(record);
    }
    settings.endGroup();
    return records;
}

void upsertAccount(const AccountRecord& record) {
    const QString username = record.username.trimmed();
    if (username.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("accounts/%1").arg(username));
    if (!record.nickname.isEmpty()) {
        settings.setValue(QStringLiteral("nickname"), record.nickname);
    }
    if (record.user_id != 0) {
        settings.setValue(QStringLiteral("user_id"), record.user_id);
    }
    settings.endGroup();
    settings.sync();
}

void forgetAccountRecord(const QString& username) {
    const QString trimmed = username.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QString avatar = accountAvatarPath(trimmed);
    if (!avatar.isEmpty()) {
        QFile::remove(avatar);
    }
    QSettings settings;
    settings.remove(QStringLiteral("accounts/%1").arg(trimmed));
    settings.sync();
}

}  // namespace chatbox::client
