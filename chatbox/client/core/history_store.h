#ifndef CHATBOX_CLIENT_CORE_HISTORY_STORE_H
#define CHATBOX_CLIENT_CORE_HISTORY_STORE_H

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include "core/chat_message.h"

namespace chatbox {
namespace client {

// 一条会话摘要，用于离线侧栏与最近会话列表。
struct ConversationRow {
    ChatContext context = ChatContext::kRoom;
    quint64 target_id = 0;
    QString title;
    quint64 last_message_id = 0;
    QString last_content;
    QDateTime last_time_utc;
};

// 本地聊天记录库（SQLite / QSQLITE）。按账号隔离：每个账号一个连接名
// history_<user_id>，所有读写 SQL 都必须带 user_id 过滤条件。
// 只在本机保存服务端已确认的消息，不保存未确认的本地待发消息。
class HistoryStore {
public:
    HistoryStore();
    ~HistoryStore();

    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    // 打开（或创建）指定目录下的本地库；目录不存在会自动创建。
    // 同一目录 + 同一账号重复调用直接返回 true；换账号或换目录先 close()。
    // 失败时写入非空中文错误文本并返回 false。
    bool open(const QString& directory, quint64 user_id, QString* error);
    void close();
    bool isOpen() const;

    QString directory() const;   // 绝对路径
    quint64 userId() const;

    qint64 sizeBytes() const;    // history.sqlite + -wal + -shm
    qint64 messageCount() const; // 当前 user_id 的消息数
    bool hasMessages() const;

    static QString defaultDirectory();

private:
    static QString connectionNameFor(quint64 user_id);

    // 连接名归属登记：同一个 history_<user_id> 同时只能被一个存活实例持有，
    // 否则第二个实例会顺手 removeDatabase 掉第一个实例的连接。
    // 只在 UI 线程使用（与设计一致），因此不需要额外加锁。
    static HistoryStore* connectionOwner(const QString& connection_name);
    static void claimConnection(const QString& connection_name, HistoryStore* owner);
    static void releaseConnection(const QString& connection_name, HistoryStore* owner);

    QString directory_;
    quint64 user_id_ = 0;
    QString connection_name_;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_HISTORY_STORE_H
