#include "ui/chat_view.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QContextMenuEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

ChatView::ChatView(QWidget* parent) : QTextBrowser(parent) {
    setMouseTracking(true);
    if (viewport() != nullptr) {
        viewport()->setMouseTracking(true);
        // The viewport fills the widget, so the scroll area itself rarely
        // receives a Leave event; listen on the viewport to reset the hover
        // state when the pointer moves to the composer or another panel.
        viewport()->installEventFilter(this);
    }
}

bool ChatView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == viewport() && event->type() == QEvent::Leave) {
        clearHoverState();
        emit hoverCleared();
    }
    return QTextBrowser::eventFilter(watched, event);
}

void ChatView::setConversationHtml(
    const QString& html,
    const QVector<chatbox::client::MessageKey>& message_keys) {
    setHtml(html);
    styleAnchors();
    buildMessageRanges(message_keys);
    hovered_id_ = 0;
}

quint64 ChatView::messageIdAt(const QPoint& viewport_pos) const {
    chatbox::client::MessageKey key;
    if (!messageKeyAt(viewport_pos, &key)) {
        return 0;
    }
    return key.message_id;
}

bool ChatView::messageKeyAt(const QPoint& viewport_pos,
                            chatbox::client::MessageKey* key) const {
    if (message_ranges_.isEmpty()) {
        return false;
    }
    const int position = cursorForPosition(viewport_pos).position();
    for (const MessageRange& range : message_ranges_) {
        if (position >= range.first && position < range.last) {
            if (key != nullptr) {
                *key = range.key;
            }
            return true;
        }
    }
    return false;
}

QRect ChatView::messageAnchorRect(quint64 message_id) const {
    if (message_id == 0) {
        return QRect();
    }
    for (const MessageRange& range : message_ranges_) {
        if (range.key.message_id != message_id) {
            continue;
        }
        const QTextBlock block =
            document()->findBlock(qMax(range.first, range.last - 1));
        if (!block.isValid()) {
            return QRect();
        }
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock);
        return cursorRect(cursor);
    }
    return QRect();
}

QRect ChatView::messageBubbleRect(quint64 message_id) const {
    if (message_id == 0) {
        return QRect();
    }
    for (const MessageRange& range : message_ranges_) {
        if (range.key.message_id != message_id || range.frame == nullptr) {
            continue;
        }
        // The renderer wraps every message in an outer full-width table whose
        // single cell holds the actual bubble table.
        QTextTable* bubble = nullptr;
        if (range.frame->rows() > 0 && range.frame->columns() > 0) {
            const QTextTableCell cell = range.frame->cellAt(0, 0);
            for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it) {
                QTextTable* inner = qobject_cast<QTextTable*>(it.currentFrame());
                if (inner != nullptr) {
                    bubble = inner;
                    break;
                }
            }
        }
        if (bubble == nullptr || document() == nullptr ||
            document()->documentLayout() == nullptr) {
            return QRect();
        }
        const QRect document_rect =
            document()->documentLayout()->frameBoundingRect(bubble).toRect();
        return document_rect.translated(-horizontalScrollBar()->value(),
                                        -verticalScrollBar()->value());
    }
    return QRect();
}

void ChatView::clearHoverState() {
    hovered_id_ = 0;
}

void ChatView::mouseMoveEvent(QMouseEvent* event) {
    QTextBrowser::mouseMoveEvent(event);
    if (event->buttons() != Qt::NoButton) {
        if (hovered_id_ != 0) {
            hovered_id_ = 0;
            emit hoverCleared();
        }
        return;
    }
    const quint64 id = messageIdAt(event->pos());
    if (id == hovered_id_) {
        return;
    }
    hovered_id_ = id;
    if (id == 0) {
        emit hoverCleared();
    } else {
        emit hoveredMessage(id, messageAnchorRect(id));
    }
}

void ChatView::mousePressEvent(QMouseEvent* event) {
    if (hovered_id_ != 0) {
        hovered_id_ = 0;
        emit hoverCleared();
    }
    QTextBrowser::mousePressEvent(event);
}

void ChatView::leaveEvent(QEvent* event) {
    // Forget which message was hovered, otherwise returning to the same
    // bubble would compare equal to the stale id and never re-emit hovered.
    clearHoverState();
    emit hoverCleared();
    QTextBrowser::leaveEvent(event);
}

void ChatView::resizeEvent(QResizeEvent* event) {
    QTextBrowser::resizeEvent(event);
    // The bar was positioned for the old geometry; reset the hover state so
    // the same message can show it again at the new position.
    clearHoverState();
    emit hoverCleared();
}

void ChatView::contextMenuEvent(QContextMenuEvent* event) {
    // Ask the window for a menu when a message bubble is under the cursor;
    // anywhere else keeps the standard copy/select-all menu.
    chatbox::client::MessageKey key;
    if (messageKeyAt(viewport()->mapFromGlobal(event->globalPos()), &key)) {
        emit messageMenuRequested(key.message_id, key.client_msg_id,
                                  event->globalPos());
        event->accept();
        return;
    }
    QTextBrowser::contextMenuEvent(event);
}

void ChatView::buildMessageRanges(
    const QVector<chatbox::client::MessageKey>& message_keys) {
    message_ranges_.clear();
    const QTextFrame* root = document() != nullptr ? document()->rootFrame() : nullptr;
    if (root == nullptr || message_keys.isEmpty()) {
        return;
    }
    QVector<QTextTable*> frames;
    for (QTextFrame::iterator it = root->begin(); !it.atEnd(); ++it) {
        QTextTable* table = qobject_cast<QTextTable*>(it.currentFrame());
        if (table != nullptr) {
            frames.append(table);
        }
    }
    if (frames.size() != message_keys.size()) {
        // The document structure and the message list disagree; disable the
        // mapping instead of offering actions for the wrong message.
        return;
    }
    message_ranges_.reserve(frames.size());
    for (int i = 0; i < frames.size(); ++i) {
        MessageRange range;
        range.first = frames.at(i)->firstPosition();
        range.last = frames.at(i)->lastPosition();
        range.key = message_keys.at(i);
        range.frame = frames.at(i);
        message_ranges_.append(range);
    }
}

void ChatView::styleAnchors() {
    QTextDocument* doc = document();
    if (doc == nullptr) {
        return;
    }
    // Collect first: merging formats invalidates block/fragment iterators.
    QVector<QPair<int, int>> ranges;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isAnchor()) {
                ranges.append(qMakePair(fragment.position(), fragment.length()));
            }
        }
    }
    if (ranges.isEmpty()) {
        return;
    }
    QTextCharFormat link_format;
    link_format.setForeground(QColor(theme::kAccent));
    link_format.setFontUnderline(false);
    for (const QPair<int, int>& range : ranges) {
        QTextCursor cursor(doc);
        cursor.setPosition(range.first);
        cursor.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(link_format);
    }
}
