#include "core/message_store.h"

#include <QSet>

#include "core/history_store.h"

namespace chatbox {
namespace client {

void MessageStore::setHistoryStore(HistoryStore* store) {
    history_ = store;
}

void MessageStore::reset(ChatContext context, quint64 target_id) {
    context_ = context;
    target_id_ = target_id;
    messages_.clear();
}

void MessageStore::replaceHistory(const QVector<ChatMessage>& history) {
    QVector<ChatMessage> adopted = history;
    QSet<QString> reconciled;
    for (ChatMessage& message : adopted) {
        if (!message.outgoing || !message.client_msg_id.isEmpty()) {
            continue;
        }
        for (const ChatMessage& local : messages_) {
            if (!local.outgoing || local.status == MessageStatus::kSent ||
                local.message_id != 0 || local.content != message.content) {
                continue;
            }
            if (local.created_at_utc.isValid() && message.created_at_utc.isValid() &&
                qAbs(local.created_at_utc.secsTo(message.created_at_utc)) > 10) {
                continue;
            }
            message.client_msg_id = local.client_msg_id;
            reconciled.insert(local.client_msg_id);
            break;
        }
    }

    QSet<quint64> history_ids;
    for (const ChatMessage& message : adopted) {
        if (message.message_id != 0) {
            history_ids.insert(message.message_id);
        }
    }

    QVector<ChatMessage> keep;
    for (const ChatMessage& message : messages_) {
        if (reconciled.contains(message.client_msg_id)) {
            continue;
        }
        if (message.status != MessageStatus::kSent || message.message_id == 0 ||
            !history_ids.contains(message.message_id)) {
            keep.push_back(message);
        }
    }

    messages_ = adopted;
    for (const ChatMessage& message : keep) {
        if (message.message_id != 0) {
            bool duplicate = false;
            for (const ChatMessage& existing : messages_) {
                if (existing.message_id == message.message_id) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
        }
        messages_.push_back(message);
    }

    // 写穿透只落服务端刚返回的这一页（adopted），不遍历内存里的历史存量：
    // 存量消息早已由 appendIncoming / markSent 落库，整批重写会造成平方级写放大。
    for (const ChatMessage& message : adopted) {
        persistMessage(message);
    }
}

void MessageStore::prependHistory(const QVector<ChatMessage>& history) {
    QVector<ChatMessage> page;
    for (const ChatMessage& message : history) {
        if (message.message_id != 0 && hasMessageId(message.message_id)) {
            continue;
        }
        page.push_back(message);
    }

    // 写穿透只落这一页更旧的历史，不遍历合并后的内存存量（避免平方级写放大）。
    for (const ChatMessage& message : page) {
        persistMessage(message);
    }

    page += messages_;
    messages_ = page;
}

void MessageStore::appendIncoming(const ChatMessage& message) {
    if (message.message_id != 0 && hasMessageId(message.message_id)) {
        return;
    }
    if (!message.client_msg_id.isEmpty() &&
        indexOfClientId(message.client_msg_id) >= 0) {
        return;
    }
    messages_.push_back(message);
    persistMessage(message);
}

void MessageStore::addPending(const ChatMessage& message) {
    if (indexOfClientId(message.client_msg_id) >= 0) {
        return;
    }
    messages_.push_back(message);
}

bool MessageStore::markSending(const QString& client_msg_id) {
    const int index = indexOfClientId(client_msg_id);
    if (index < 0) {
        return false;
    }
    messages_[index].status = MessageStatus::kSending;
    return true;
}

bool MessageStore::markSent(const QString& client_msg_id, quint64 message_id,
                            const QDateTime& created_at_utc) {
    const int index = indexOfClientId(client_msg_id);
    if (index < 0) {
        return false;
    }
    messages_[index].message_id = message_id;
    if (created_at_utc.isValid()) {
        messages_[index].created_at_utc = created_at_utc;
    }
    messages_[index].status = MessageStatus::kSent;

    if (message_id != 0) {
        for (int i = messages_.size() - 1; i >= 0; --i) {
            if (i == index) {
                continue;
            }
            if (messages_.at(i).message_id == message_id) {
                messages_.removeAt(i);
            }
        }
    }

    // 去重会改变下标，重新按 client_msg_id 定位后再写穿透，
    // 此时消息已带上服务端 message_id 且状态为 kSent。
    const int final_index = indexOfClientId(client_msg_id);
    if (final_index >= 0) {
        persistMessage(messages_.at(final_index));
    }
    return true;
}

bool MessageStore::markFailed(const QString& client_msg_id) {
    const int index = indexOfClientId(client_msg_id);
    if (index < 0) {
        return false;
    }
    messages_[index].status = MessageStatus::kFailed;
    return true;
}

int MessageStore::markAllSendingFailed() {
    int changed = 0;
    for (ChatMessage& message : messages_) {
        if (message.status == MessageStatus::kSending) {
            message.status = MessageStatus::kFailed;
            ++changed;
        }
    }
    return changed;
}

bool MessageStore::markRecalled(quint64 message_id) {
    if (message_id == 0) {
        return false;
    }
    for (ChatMessage& message : messages_) {
        if (message.message_id == message_id) {
            message.recalled = true;
            message.content.clear();
            // 内存先置位，再同步本地历史库：本地副本同样清空正文。
            // 同步失败只记日志，不影响内存状态与界面。
            if (history_ != nullptr && !history_->markRecalled(message_id)) {
                qWarning("history recall sync failed message_id=%llu",
                         static_cast<unsigned long long>(message_id));
            }
            return true;
        }
    }
    return false;
}

const ChatMessage* MessageStore::findMessage(quint64 message_id) const {
    if (message_id == 0) {
        return nullptr;
    }
    for (const ChatMessage& message : messages_) {
        if (message.message_id == message_id) {
            return &message;
        }
    }
    return nullptr;
}

const ChatMessage* MessageStore::findByClientId(const QString& client_msg_id) const {
    const int index = indexOfClientId(client_msg_id);
    if (index < 0) {
        return nullptr;
    }
    return &messages_.at(index);
}

bool MessageStore::isCurrent(ChatContext context, quint64 target_id) const {
    return context_ == context && target_id_ == target_id;
}

const QVector<ChatMessage>& MessageStore::messages() const {
    return messages_;
}

int MessageStore::indexOfClientId(const QString& client_msg_id) const {
    if (client_msg_id.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_.at(i).client_msg_id == client_msg_id) {
            return i;
        }
    }
    return -1;
}

bool MessageStore::hasMessageId(quint64 message_id) const {
    if (message_id == 0) {
        return false;
    }
    for (const ChatMessage& message : messages_) {
        if (message.message_id == message_id) {
            return true;
        }
    }
    return false;
}

void MessageStore::persistMessage(const ChatMessage& message) const {
    if (history_ == nullptr || message.message_id == 0 ||
        message.status != MessageStatus::kSent) {
        return;
    }
    if (!history_->saveMessage(message)) {
        qWarning("history write failed message_id=%llu",
                 static_cast<unsigned long long>(message.message_id));
    }
}

}  // namespace client
}  // namespace chatbox
