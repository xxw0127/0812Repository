#include "core/autostart.h"

#include <QDir>
#include <QSettings>

namespace chatbox {
namespace client {

namespace {

#ifdef Q_OS_WIN
const char* const kRunKeyPath =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif
const char* const kRunValueName = "ChatBox";

}  // namespace

QString autostartCommand(const QString& exe_path) {
    return QStringLiteral("\"%1\" --minimized")
        .arg(QDir::toNativeSeparators(exe_path));
}

void applyAutostartSetting(QSettings& run_key, bool enabled,
                           const QString& exe_path) {
    if (enabled) {
        run_key.setValue(QString::fromLatin1(kRunValueName), autostartCommand(exe_path));
    } else {
        run_key.remove(QString::fromLatin1(kRunValueName));
    }
    run_key.sync();
}

bool setAutostartEnabled(bool enabled, const QString& exe_path) {
#ifdef Q_OS_WIN
    QSettings run_key(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    applyAutostartSetting(run_key, enabled, exe_path);
    return run_key.status() == QSettings::NoError;
#else
    Q_UNUSED(enabled);
    Q_UNUSED(exe_path);
    return false;
#endif
}

bool isAutostartEnabled() {
#ifdef Q_OS_WIN
    QSettings run_key(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    return !run_key.value(QString::fromLatin1(kRunValueName)).toString().isEmpty();
#else
    return false;
#endif
}

}  // namespace client
}  // namespace chatbox
