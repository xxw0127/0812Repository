#include "core/client_log.h"
#include "core/socket_compat.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QStyleFactory>

#include "ui/main_window.h"

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("Chatroom");
    QCoreApplication::setApplicationName("ChatroomClient");
    QCoreApplication::setApplicationVersion("0.2.0");
    application.setStyle(QStyleFactory::create("Fusion"));
    application.setWindowIcon(QIcon(":/app.ico"));
    QFile style_file(":/style.qss");
    if (style_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application.setStyleSheet(QString::fromUtf8(style_file.readAll()));
    }
    installClientLogging();
    qInfo("client starting version=%s", qPrintable(QCoreApplication::applicationVersion()));

    std::string error;
    if (!chatroom::initializeSockets(error)) {
        QMessageBox::critical(nullptr, "Socket init failed", QString::fromStdString(error));
        return 1;
    }

    MainWindow window;
    window.show();
    const int result = application.exec();
    chatroom::cleanupSockets();
    return result;
}
