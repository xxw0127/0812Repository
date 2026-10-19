#include "core/message_renderer.h"

#include <QStringList>

#include "core/message_time.h"
#include "core/ui_theme.h"

namespace chatbox {
namespace client {

namespace {

QString senderLabel(const ChatMessage& message) {
    if (!message.sender_name.isEmpty()) {
        return message.sender_name;
    }
    return QString::number(message.sender_id);
}

QString messageMeta(const ChatMessage& message) {
    QString meta = formatLocalTime(message.created_at_utc);
    switch (message.status) {
        case MessageStatus::kSending:
            meta += "  发送中...";
            break;
        case MessageStatus::kFailed:
            meta += "  <span style=\"color: " +
                    QString::fromLatin1(theme::kSemanticDown) + ";\">发送失败</span> <a href=\"retry:" +
                    message.client_msg_id.toHtmlEscaped() +
                    "\" style=\"color: " + QString::fromLatin1(theme::kAccent) +
                    "; text-decoration: none;\">重发</a>";
            break;
        case MessageStatus::kSent:
            break;
    }
    return meta;
}

}  // namespace

MessageActions messageActions(const ChatMessage& message, quint64 self_user_id,
                              const QDateTime& now_utc) {
    MessageActions actions;
    const bool outgoing =
        message.outgoing || (message.sender_id != 0 && message.sender_id == self_user_id);
    if (message.status == MessageStatus::kFailed && !message.client_msg_id.isEmpty()) {
        actions.can_retry = true;
    }
    if (message.message_id != 0 && !message.recalled) {
        actions.can_reply = true;
        const bool within_window =
            message.created_at_utc.isValid() &&
            message.created_at_utc.secsTo(now_utc) <= kRecallWindowSeconds;
        actions.can_recall = outgoing && within_window;
    }
    return actions;
}

QString renderConversationHtml(const QVector<ChatMessage>& messages,
                               quint64 self_user_id,
                               QVector<MessageKey>* rendered_messages) {
    if (rendered_messages != nullptr) {
        rendered_messages->clear();
    }
    QStringList html;
    html << "<html><body style=\"font-family: 'Microsoft YaHei', sans-serif;\">";

    QString current_date;
    for (const ChatMessage& message : messages) {
        if (rendered_messages != nullptr) {
            MessageKey key;
            key.message_id = message.message_id;
            key.client_msg_id = message.client_msg_id;
            rendered_messages->append(key);
        }
        const QString date = formatLocalDate(message.created_at_utc);
        if (date != current_date) {
            current_date = date;
            html << "<div align=\"center\" style=\"color: " +
                        QString::fromLatin1(theme::kMuted) +
                        "; font-size: 12px; margin: 12px 0;\">"
                 << date.toHtmlEscaped() << "</div>";
        }

        const bool outgoing =
            message.outgoing || (message.sender_id != 0 && message.sender_id == self_user_id);
        const QString alignment = outgoing ? "right" : "left";
        const QString background = outgoing ? QString::fromLatin1(theme::kBubbleOwn)
                                            : QString::fromLatin1(theme::kBubbleOther);

        QString content = message.recalled ? "消息已撤回"
                                           : message.content.toHtmlEscaped();
        content.replace("\n", "<br>");

        html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\"><tr><td align=\""
             << alignment << "\">";
        html << "<table cellspacing=\"0\" cellpadding=\"8\" bgcolor=\"" << background
             << "\"><tr><td>";
        if (!outgoing) {
            html << "<div style=\"color: " + QString::fromLatin1(theme::kBody) +
                        "; font-size: 12px;\">"
                 << senderLabel(message).toHtmlEscaped() << "</div>";
        }
        if (message.reply_to_id != 0) {
            QString quote = message.reply_content.toHtmlEscaped();
            quote.replace("\n", "<br>");
            if (quote.isEmpty()) {
                quote = "消息已撤回";
            }
            html << "<div style=\"border-left: 3px solid " +
                        QString::fromLatin1(theme::kHairline) +
                        "; padding-left: 6px; color: " +
                        QString::fromLatin1(theme::kMuted) +
                        "; font-size: 12px; margin-bottom: 4px;\">回复 "
                 << (message.reply_sender_name.isEmpty()
                         ? QString()
                         : message.reply_sender_name.toHtmlEscaped() + ": ")
                 << quote << "</div>";
        }
        if (message.recalled) {
            html << "<div style=\"font-size: 14px; color: " +
                        QString::fromLatin1(theme::kMutedSoft) + ";\"><i>"
                 << content << "</i></div>";
        } else {
            html << "<div style=\"font-size: 14px;\">" << content << "</div>";
        }
        html << "<div style=\"color: " + QString::fromLatin1(theme::kMutedSoft) +
                    "; font-size: 11px; text-align: right;\">"
             << messageMeta(message) << "</div>";
        html << "</td></tr></table></td></tr></table>";
    }

    html << "</body></html>";
    return html.join(QString());
}

}  // namespace client
}  // namespace chatbox
