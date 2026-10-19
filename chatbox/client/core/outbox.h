#ifndef CHATBOX_CLIENT_CORE_OUTBOX_H
#define CHATBOX_CLIENT_CORE_OUTBOX_H

#include <QDateTime>
#include <QString>
#include <QVector>

#include "core/chat_message.h"

namespace chatbox {
namespace client {

struct OutboxEntry {
    ChatMessage message;
    QDateTime last_attempt_at;
    int attempts = 0;
};

class Outbox {
public:
    void add(const ChatMessage& message, const QDateTime& now);
    bool markSending(const QString& client_msg_id, const QDateTime& now);
    bool markSent(const QString& client_msg_id);
    bool markFailed(const QString& client_msg_id);
    int markAllSendingFailed();
    QVector<QString> expireSending(const QDateTime& now, int timeout_seconds);
    bool contains(const QString& client_msg_id) const;
    QVector<ChatMessage> pending() const;
    void clear();

private:
    int indexOf(const QString& client_msg_id) const;

    QVector<OutboxEntry> entries_;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_OUTBOX_H
