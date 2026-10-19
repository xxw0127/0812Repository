#ifndef CHATROOM_CLIENT_CORE_MESSAGE_STORE_H
#define CHATROOM_CLIENT_CORE_MESSAGE_STORE_H

#include <QDateTime>
#include <QString>
#include <QVector>

#include "core/chat_message.h"

namespace chatroom {
namespace client {

class MessageStore {
public:
    void reset(ChatContext context, quint64 target_id);
    void replaceHistory(const QVector<ChatMessage>& history);
    void prependHistory(const QVector<ChatMessage>& history);
    void appendIncoming(const ChatMessage& message);
    void addPending(const ChatMessage& message);

    bool markSending(const QString& client_msg_id);
    bool markSent(const QString& client_msg_id, quint64 message_id,
                  const QDateTime& created_at_utc);
    bool markFailed(const QString& client_msg_id);
    int markAllSendingFailed();
    bool markRecalled(quint64 message_id);
    const ChatMessage* findMessage(quint64 message_id) const;
    const ChatMessage* findByClientId(const QString& client_msg_id) const;

    bool isCurrent(ChatContext context, quint64 target_id) const;
    const QVector<ChatMessage>& messages() const;

private:
    int indexOfClientId(const QString& client_msg_id) const;
    bool hasMessageId(quint64 message_id) const;

    ChatContext context_ = ChatContext::kRoom;
    quint64 target_id_ = 0;
    QVector<ChatMessage> messages_;
};

}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_MESSAGE_STORE_H
