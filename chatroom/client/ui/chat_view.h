#ifndef CHATROOM_CLIENT_UI_CHAT_VIEW_H
#define CHATROOM_CLIENT_UI_CHAT_VIEW_H

#include <QRect>
#include <QTextBrowser>
#include <QVector>

#include "core/chat_message.h"

class QTextTable;

// Read-only conversation view. Besides rendering the conversation it maps
// viewport positions back to messages so the window can offer hover actions
// and a per-message context menu. After every render it also normalises link
// styling (theme blue, no underline).
class ChatView : public QTextBrowser {
    Q_OBJECT

public:
    explicit ChatView(QWidget* parent = nullptr);

    // Replaces the document and rebuilds the position -> message map.
    void setConversationHtml(const QString& html,
                             const QVector<chatroom::client::MessageKey>& message_keys);

    // Server message id at a viewport position, or 0 when the position is
    // not inside a mapped message bubble or the bubble has no server id yet.
    quint64 messageIdAt(const QPoint& viewport_pos) const;

    // Full key (server id or client message id) of the bubble at a viewport
    // position; false when the position is not on a message.
    bool messageKeyAt(const QPoint& viewport_pos,
                      chatroom::client::MessageKey* key) const;

    // Rect (viewport coordinates) of the message's meta line (time/status),
    // used to place the hover action bar. Null when the id is unknown.
    QRect messageAnchorRect(quint64 message_id) const;

    // Rect (viewport coordinates) of the message's bubble itself. Used to
    // place the hover action bar next to the bubble. Null when the id is
    // unknown or the document structure does not expose the bubble.
    QRect messageBubbleRect(quint64 message_id) const;

    // Forgets the currently hovered message without emitting anything; the
    // next mouse move over the same message reports it again.
    void clearHoverState();

signals:
    void hoveredMessage(quint64 message_id, const QRect& meta_rect);
    void hoverCleared();
    void messageMenuRequested(quint64 message_id, const QString& client_msg_id,
                              const QPoint& global_pos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct MessageRange {
        int first = 0;
        int last = 0;
        chatroom::client::MessageKey key;
        QTextTable* frame = nullptr;
    };

    void buildMessageRanges(const QVector<chatroom::client::MessageKey>& message_keys);
    void styleAnchors();

    QVector<MessageRange> message_ranges_;
    quint64 hovered_id_ = 0;
};

#endif  // CHATROOM_CLIENT_UI_CHAT_VIEW_H
