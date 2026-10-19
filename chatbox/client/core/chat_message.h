#ifndef CHATROOM_CLIENT_CORE_CHAT_MESSAGE_H
#define CHATROOM_CLIENT_CORE_CHAT_MESSAGE_H

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <cstdint>

namespace chatroom {
namespace client {

enum class ChatContext {
    kRoom = 0,
    kPrivate = 1,
};

enum class MessageStatus {
    kSending = 0,
    kSent = 1,
    kFailed = 2,
};

struct ChatMessage {
    QString client_msg_id;
    quint64 message_id = 0;
    ChatContext context = ChatContext::kRoom;
    quint64 target_id = 0;
    quint64 sender_id = 0;
    QString sender_name;
    QString content;
    QDateTime created_at_utc;
    uint64_t reply_to_id = 0;
    QString reply_sender_name;
    QString reply_content;
    bool recalled = false;
    bool outgoing = false;
    MessageStatus status = MessageStatus::kSent;
};

// Identifies one rendered conversation bubble: the server message id when
// the message has one, otherwise the client message id of a message that is
// still pending or failed.
struct MessageKey {
    quint64 message_id = 0;
    QString client_msg_id;

    bool isNull() const { return message_id == 0 && client_msg_id.isEmpty(); }
};

}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_CHAT_MESSAGE_H
