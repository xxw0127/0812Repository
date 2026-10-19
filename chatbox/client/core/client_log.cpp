#include "core/client_log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <QThread>
#include <QtGlobal>

namespace {

constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;

QMutex g_log_mutex;
QFile g_log_file;
QString g_log_path;
bool g_installed = false;

QString logPath() {
    if (!g_log_path.isEmpty()) {
        return g_log_path;
    }
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
    g_log_path = directory + "/client.log";
    return g_log_path;
}

bool openLogFile() {
    const QString path = logPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    g_log_file.setFileName(path);
    return g_log_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void rotateIfNeeded() {
    if (!g_log_file.isOpen()) {
        return;
    }
    if (g_log_file.size() <= kMaxLogBytes) {
        return;
    }
    const QString path = logPath();
    g_log_file.close();
    QFile::remove(path + ".1");
    QFile::rename(path, path + ".1");
    openLogFile();
}

const char* levelName(QtMsgType type) {
    switch (type) {
        case QtDebugMsg:
            return "DEBUG";
        case QtInfoMsg:
            return "INFO";
        case QtWarningMsg:
            return "WARN";
        case QtCriticalMsg:
            return "ERROR";
        case QtFatalMsg:
            return "FATAL";
    }
    return "UNKNOWN";
}

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    QMutexLocker locker(&g_log_mutex);
    if (!g_log_file.isOpen()) {
        openLogFile();
    }
    if (!g_log_file.isOpen()) {
        return;
    }
    rotateIfNeeded();
    const QString line =
        QString("%1 [%2] %3 %4\n")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"))
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
            .arg(levelName(type), message);
    g_log_file.write(line.toUtf8());
    g_log_file.flush();
}

}  // namespace

void installClientLogging() {
    if (g_installed) {
        return;
    }
    QMutexLocker locker(&g_log_mutex);
    if (openLogFile()) {
        g_installed = true;
        qInstallMessageHandler(messageHandler);
    }
}

QString clientLogFilePath() {
    return logPath();
}
