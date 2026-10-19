#include "core/outbox.h"

namespace chatbox {
namespace client {

void Outbox::add(const ChatMessage& message, const QDateTime& now) {
    if (indexOf(message.client_msg_id) >= 0) {
        return;
    }
    OutboxEntry entry;
    entry.message = message;
    entry.message.status = MessageStatus::kSending;
    entry.last_attempt_at = now;
    entry.attempts = 1;
    entries_.push_back(entry);
}

bool Outbox::markSending(const QString& client_msg_id, const QDateTime& now) {
    const int index = indexOf(client_msg_id);
    if (index < 0) {
        return false;
    }
    entries_[index].message.status = MessageStatus::kSending;
    entries_[index].last_attempt_at = now;
    ++entries_[index].attempts;
    return true;
}

bool Outbox::markSent(const QString& client_msg_id) {
    const int index = indexOf(client_msg_id);
    if (index < 0) {
        return false;
    }
    entries_.removeAt(index);
    return true;
}

bool Outbox::markFailed(const QString& client_msg_id) {
    const int index = indexOf(client_msg_id);
    if (index < 0) {
        return false;
    }
    entries_[index].message.status = MessageStatus::kFailed;
    return true;
}

int Outbox::markAllSendingFailed() {
    int changed = 0;
    for (OutboxEntry& entry : entries_) {
        if (entry.message.status == MessageStatus::kSending) {
            entry.message.status = MessageStatus::kFailed;
            ++changed;
        }
    }
    return changed;
}

QVector<QString> Outbox::expireSending(const QDateTime& now, int timeout_seconds) {
    QVector<QString> expired;
    if (!now.isValid() || timeout_seconds <= 0) {
        return expired;
    }
    for (OutboxEntry& entry : entries_) {
        if (entry.message.status != MessageStatus::kSending ||
            !entry.last_attempt_at.isValid()) {
            continue;
        }
        if (entry.last_attempt_at.secsTo(now) >= timeout_seconds) {
            entry.message.status = MessageStatus::kFailed;
            expired.push_back(entry.message.client_msg_id);
        }
    }
    return expired;
}

bool Outbox::contains(const QString& client_msg_id) const {
    return indexOf(client_msg_id) >= 0;
}

QVector<ChatMessage> Outbox::pending() const {
    QVector<ChatMessage> result;
    result.reserve(entries_.size());
    for (const OutboxEntry& entry : entries_) {
        result.push_back(entry.message);
    }
    return result;
}

void Outbox::clear() {
    entries_.clear();
}

int Outbox::indexOf(const QString& client_msg_id) const {
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_.at(i).message.client_msg_id == client_msg_id) {
            return i;
        }
    }
    return -1;
}

}  // namespace client
}  // namespace chatbox
