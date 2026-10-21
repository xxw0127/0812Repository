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

bool shouldShowSenderName(const ChatMessage* previous, const ChatMessage& current) {
    if (previous == nullptr || current.recalled) {
        return false;
    }
    if (previous->sender_id != current.sender_id) {
        return true;
    }
    if (!previous->created_at_utc.isValid() || !current.created_at_utc.isValid()) {
        return true;
    }
    const qint64 seconds =
        previous->created_at_utc.secsTo(current.created_at_utc);
    return seconds < 0 || seconds >= 180;
}

QString renderMessageFragment(const ChatMessage& message, quint64 self_user_id,
                              bool include_date_header, const QString& date_label,
                              bool show_sender_name) {
    QString html;
    if (include_date_header) {
        html += "<div align=\"center\" style=\"color: " +
                QString::fromLatin1(theme::kMuted) +
                "; font-size: 11px; margin: 16px 0;\">" +
                date_label.toHtmlEscaped() + "</div>";
    }

    const bool outgoing =
        message.outgoing || (message.sender_id != 0 && message.sender_id == self_user_id);
    const QString alignment = outgoing ? "right" : "left";
    QString background = outgoing ? QString::fromLatin1(theme::kBubbleOwn)
                                  : QString::fromLatin1(theme::kBubbleOther);
    if (message.recalled) {
        background = QString::fromLatin1(theme::kSurfaceSoft);
    }
    const QString quote_background = QString::fromLatin1(theme::kQuoteBg);

    QString content = message.recalled ? "消息已撤回" : message.content.toHtmlEscaped();
    content.replace("\n", "<br>");

    const int top_margin = show_sender_name ? 12 : 4;
    html += "<div style=\"margin-top: " + QString::number(top_margin) + "px;\">";
    // Nested tables are the only reliable way to simulate a 1px border in
    // Qt's rich-text engine (border-radius and box-shadow are unsupported).
    html += "<table align=\"" + alignment +
            "\" cellspacing=\"0\" cellpadding=\"1\" bgcolor=\"" +
            QString::fromLatin1(theme::kHairline) + "\"><tr><td>";
    html += "<table cellspacing=\"0\" cellpadding=\"10\" bgcolor=\"" + background +
            "\"><tr><td>";
    if (!outgoing && !message.recalled && show_sender_name) {
        html += "<div style=\"color: " + QString::fromLatin1(theme::kBody) +
                "; font-size: 12px; margin-bottom: 2px;\">" +
                senderLabel(message).toHtmlEscaped() + "</div>";
    }
    if (message.reply_to_id != 0) {
        QString quote = message.reply_content.toHtmlEscaped();
        quote.replace("\n", "<br>");
        if (quote.isEmpty()) {
            quote = "消息已撤回";
        }
        html += "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" bgcolor=\"" +
                quote_background + "\"><tr><td><div style=\"color: " +
                QString::fromLatin1(theme::kMuted) +
                "; font-size: 12px;\">回复 " +
                (message.reply_sender_name.isEmpty()
                     ? QString()
                     : message.reply_sender_name.toHtmlEscaped() + ": ") +
                quote + "</div></td></tr></table>";
        html += "<div style=\"font-size: 6px; line-height: 6px;\">&nbsp;</div>";
    }
    if (message.recalled) {
        html += "<div style=\"font-size: 14px; color: " +
                QString::fromLatin1(theme::kMutedSoft) + ";\"><i>" + content +
                "</i></div>";
    } else {
        html += "<div style=\"font-size: 14px; line-height: 150%;\">" + content +
                "</div>";
    }
    html += "</td></tr></table>";
    html += "</td></tr></table>";
    html += "<div style=\"color: " + QString::fromLatin1(theme::kMutedSoft) +
            "; font-size: 11px; margin: 4px 4px 8px 4px; text-align: " +
            (outgoing ? "right" : "left") + ";\">" + messageMeta(message) + "</div>";
    html += "</div>";
    return html;
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
    for (int index = 0; index < messages.size(); ++index) {
        const ChatMessage& message = messages.at(index);
        if (rendered_messages != nullptr) {
            MessageKey key;
            key.message_id = message.message_id;
            key.client_msg_id = message.client_msg_id;
            rendered_messages->append(key);
        }
        const QString date = formatLocalDate(message.created_at_utc);
        const bool include_date_header = date != current_date;
        if (include_date_header) {
            current_date = date;
        }
        const ChatMessage* previous = index > 0 ? &messages.at(index - 1) : nullptr;
        const bool show_sender_name =
            include_date_header || shouldShowSenderName(previous, message);
        html << renderMessageFragment(message, self_user_id, include_date_header, date,
                                      show_sender_name);
    }

    html << "</body></html>";
    return html.join(QString());
}

}  // namespace client
}  // namespace chatbox
