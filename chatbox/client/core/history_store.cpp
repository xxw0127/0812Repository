#include "core/history_store.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace chatbox {
namespace client {

namespace {

const char* const kDatabaseFileName = "history.sqlite";
const int kSchemaVersion = 1;

// 建库语句与设计文档 §5.2 完全一致（注释一并保留，便于逐条比对）。
const char* const kCreateMessagesTable =
    R"(CREATE TABLE IF NOT EXISTS messages (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id           INTEGER NOT NULL,          -- 归属账号（本机隔离）
    context           INTEGER NOT NULL,          -- 0 房间 / 1 私聊
    target_id         INTEGER NOT NULL,          -- room_id 或 peer_id
    message_id        INTEGER NOT NULL,          -- 服务端消息 id（只存已确认消息）
    sender_id         INTEGER NOT NULL,
    sender_name       TEXT    NOT NULL DEFAULT '',
    content           TEXT    NOT NULL,
    created_at_utc    TEXT    NOT NULL,          -- ISO8601 UTC
    reply_to_id       INTEGER NOT NULL DEFAULT 0,
    reply_sender_name TEXT    NOT NULL DEFAULT '',
    reply_content     TEXT    NOT NULL DEFAULT '',
    recalled          INTEGER NOT NULL DEFAULT 0,
    outgoing          INTEGER NOT NULL DEFAULT 0
);
)";

const char* const kCreateUniqueIndex =
    R"(CREATE UNIQUE INDEX IF NOT EXISTS uk_msg_server
    ON messages(user_id, context, target_id, message_id);
)";

const char* const kCreatePageIndex =
    R"(CREATE INDEX IF NOT EXISTS idx_msg_page
    ON messages(user_id, context, target_id, message_id);
)";

const char* const kCreateConversationsTable =
    R"(CREATE TABLE IF NOT EXISTS conversations (
    user_id        INTEGER NOT NULL,
    context        INTEGER NOT NULL,
    target_id      INTEGER NOT NULL,
    title          TEXT    NOT NULL,             -- 房间名/昵称/备注
    last_message_id INTEGER NOT NULL DEFAULT 0,
    last_content   TEXT    NOT NULL DEFAULT '',
    last_time_utc  TEXT    NOT NULL DEFAULT '',
    PRIMARY KEY (user_id, context, target_id)
);
)";

QString databasePath(const QString& directory) {
    return QDir(directory).filePath(QString::fromLatin1(kDatabaseFileName));
}

qint64 fileSizeOf(const QString& path) {
    const QFileInfo info(path);
    return info.exists() ? info.size() : 0;
}

// 执行单条 DDL / PRAGMA；失败时写入中文错误文本。
bool runStatement(QSqlDatabase& db, const QString& label, const QString& sql,
                  QString* error) {
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        if (error) {
            *error = QStringLiteral("本地历史库初始化失败（%1）：%2")
                         .arg(label, query.lastError().text());
        }
        return false;
    }
    return true;
}

// 建表 / 建索引 / 写入 user_version。所有语句都要求幂等，可在已有库上重跑。
bool applySchema(QSqlDatabase& db, QString* error) {
    if (!runStatement(db, QStringLiteral("日志模式"),
                      QStringLiteral("PRAGMA journal_mode=WAL"), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("同步级别"),
                      QStringLiteral("PRAGMA synchronous=NORMAL"), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("消息表"),
                      QString::fromUtf8(kCreateMessagesTable), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("唯一索引"),
                      QString::fromUtf8(kCreateUniqueIndex), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("分页索引"),
                      QString::fromUtf8(kCreatePageIndex), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("会话表"),
                      QString::fromUtf8(kCreateConversationsTable), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("版本号"),
                      QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion),
                      error)) {
        return false;
    }
    return true;
}

// 移除同名残留连接（没有任何存活实例持有的历史残留）。
void dropConnection(const QString& connection_name) {
    if (!QSqlDatabase::contains(connection_name)) {
        return;
    }
    {
        QSqlDatabase stale = QSqlDatabase::database(connection_name, false);
        if (stale.isValid()) {
            stale.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name);
}

// 进程内每个连接名的归属表：连接名 -> 持有它的 HistoryStore 实例。
// 只在 UI 线程访问（与设计一致），因此不需要额外加锁。
QHash<QString, HistoryStore*>& connectionOwners() {
    static QHash<QString, HistoryStore*> owners;
    return owners;
}

}  // namespace

HistoryStore::HistoryStore() = default;

HistoryStore::~HistoryStore() {
    close();
}

QString HistoryStore::connectionNameFor(quint64 user_id) {
    return QStringLiteral("history_") + QString::number(user_id);
}

HistoryStore* HistoryStore::connectionOwner(const QString& connection_name) {
    return connectionOwners().value(connection_name, nullptr);
}

void HistoryStore::claimConnection(const QString& connection_name,
                                   HistoryStore* owner) {
    connectionOwners().insert(connection_name, owner);
}

void HistoryStore::releaseConnection(const QString& connection_name,
                                     HistoryStore* owner) {
    if (connectionOwners().value(connection_name, nullptr) == owner) {
        connectionOwners().remove(connection_name);
    }
}

bool HistoryStore::open(const QString& directory, quint64 user_id, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (user_id == 0) {
        return fail(QStringLiteral("本地历史库账号无效"));
    }
    if (directory.trimmed().isEmpty()) {
        return fail(QStringLiteral("本地历史库目录为空"));
    }

    const QString target = QDir(directory).absolutePath();
    const QFileInfo target_info(target);
    if (target_info.exists() && !target_info.isDir()) {
        return fail(QStringLiteral("本地历史库目录不是文件夹：%1").arg(target));
    }
    if (!QDir().mkpath(target)) {
        return fail(QStringLiteral("本地历史库目录创建失败：%1").arg(target));
    }
    if (!QFileInfo(target).isDir()) {
        return fail(QStringLiteral("本地历史库目录不可用：%1").arg(target));
    }

    // 同目录同账号重复打开：直接复用现有连接。
    if (isOpen() && directory_ == target && user_id_ == user_id) {
        if (error) {
            error->clear();
        }
        return true;
    }
    // 换账号 / 换目录 / 连接已失效：先释放旧连接。
    close();

    const QString connection_name = connectionNameFor(user_id);
    // 同一个连接名同时只能被一个存活实例持有：若已被别的实例占用，
    // 必须直接失败，既不能 addDatabase 也不能 removeDatabase，
    // 否则会把对方的连接拆掉，让对方读到/写到这个库文件上。
    HistoryStore* owner = connectionOwner(connection_name);
    if (owner != nullptr && owner != this) {
        return fail(QStringLiteral("本地历史库已被其它实例占用：%1")
                        .arg(connection_name));
    }
    // 清理没有存活实例持有的残留连接。
    dropConnection(connection_name);

    QString failure;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    connection_name);
        db.setDatabaseName(databasePath(target));
        if (!db.open()) {
            failure = QStringLiteral("本地历史库打开失败：%1")
                          .arg(db.lastError().text());
        } else {
            applySchema(db, &failure);
        }
        // 失败时立即关闭；成功时保持连接，供后续读写复用。
        if (!failure.isEmpty()) {
            db.close();
        }
    }
    if (!failure.isEmpty()) {
        dropConnection(connection_name);
        return fail(failure);
    }

    directory_ = target;
    user_id_ = user_id;
    connection_name_ = connection_name;
    claimConnection(connection_name, this);
    if (error) {
        error->clear();
    }
    return true;
}

void HistoryStore::close() {
    if (!connection_name_.isEmpty()) {
        // 只释放自己登记的连接，别人的连接一律不碰。
        if (connectionOwner(connection_name_) == this) {
            releaseConnection(connection_name_, this);
            dropConnection(connection_name_);
        }
    }
    connection_name_.clear();
    directory_.clear();
    user_id_ = 0;
}

bool HistoryStore::isOpen() const {
    if (connection_name_.isEmpty()) {
        return false;
    }
    const QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    return db.isValid() && db.isOpen();
}

QString HistoryStore::directory() const {
    return directory_;
}

quint64 HistoryStore::userId() const {
    return user_id_;
}

qint64 HistoryStore::sizeBytes() const {
    if (directory_.isEmpty()) {
        return 0;
    }
    const QString base = databasePath(directory_);
    qint64 total = 0;
    total += fileSizeOf(base);
    total += fileSizeOf(base + QStringLiteral("-wal"));
    total += fileSizeOf(base + QStringLiteral("-shm"));
    return total;
}

qint64 HistoryStore::messageCount() const {
    if (!isOpen()) {
        return 0;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM messages WHERE user_id = ?"))) {
        return 0;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

bool HistoryStore::hasMessages() const {
    return messageCount() > 0;
}

QString HistoryStore::defaultDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/history");
}

}  // namespace client
}  // namespace chatbox
