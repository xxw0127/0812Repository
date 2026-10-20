#include "core/app_settings.h"
#include "core/client_log.h"
#include "core/socket_compat.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSystemTrayIcon>

#include "ui/main_window.h"

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("ChatBox");
    QCoreApplication::setApplicationName("ChatBox");
    QCoreApplication::setApplicationVersion("0.2.0");

    // One instance only: two clients would race on the same settings store and
    // (from the history batch on) the same local database. A crashed instance
    // leaves a stale lock that expires instead of blocking forever.
    const QString lock_directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(lock_directory);
    QLockFile instance_lock(QDir(lock_directory).filePath(QStringLiteral("chatbox.lock")));
    instance_lock.setStaleLockTime(30000);
    if (!instance_lock.tryLock(200)) {
        QMessageBox::information(nullptr, "ChatBox", "ChatBox 已在运行。");
        return 0;
    }

    application.setStyle(QStyleFactory::create("Fusion"));
    application.setWindowIcon(QIcon(":/app.ico"));
    QFile style_file(":/style.qss");
    if (style_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application.setStyleSheet(QString::fromUtf8(style_file.readAll()));
    }
    installClientLogging();
    qInfo("client starting version=%s", qPrintable(QCoreApplication::applicationVersion()));

    // Sessions and preferences used to live under the Chatroom/ChatroomClient
    // identifiers; copy them once so the ChatBox rename does not log everyone
    // out. The legacy store is left in place so a rollback keeps working.
    {
        QSettings current;
        QSettings legacy(QSettings::UserScope, QStringLiteral("Chatroom"),
                         QStringLiteral("ChatroomClient"));
        if (chatbox::client::migrateLegacySettings(current, legacy)) {
            qInfo("settings migrated from the pre-ChatBox store");
        }
    }

    std::string error;
    if (!chatbox::initializeSockets(error)) {
        QMessageBox::critical(nullptr, "Socket init failed", QString::fromStdString(error));
        return 1;
    }

    const bool start_minimized =
        QCoreApplication::arguments().contains(QStringLiteral("--minimized"));
    MainWindow window;
    if (!start_minimized || !QSystemTrayIcon::isSystemTrayAvailable()) {
        window.show();
    }
    const int result = application.exec();
    chatbox::cleanupSockets();
    return result;
}
