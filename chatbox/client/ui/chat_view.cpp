#include "ui/chat_view.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QContextMenuEvent>
#include <QEvent>
#include <QKeyEvent>
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
    // QTextBrowser defaults to ClickFocus; keyboard users need to reach the
    // message menu through the normal Tab chain.
    setFocusPolicy(Qt::StrongFocus);
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

void ChatView::appendFragment(const QString& html,
                              const chatbox::client::MessageKey& key) {
    QTextDocument* doc = document();
    if (doc == nullptr || html.isEmpty()) {
        return;
    }

    // Appending at the document end cannot move any existing range: all old
    // positions stay before the insertion point. This is the safety property
    // that lets the incremental path skip buildMessageRanges().
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    const int start = cursor.position();
    cursor.insertHtml(html);
    const int end = cursor.position();

    styleAnchorsInRange(start, end);

    QTextTable* table = nullptr;
    const QTextFrame* root = doc->rootFrame();
    if (root != nullptr) {
        for (QTextFrame::iterator it = root->begin(); !it.atEnd(); ++it) {
            auto* candidate = qobject_cast<QTextTable*>(it.currentFrame());
            if (candidate != nullptr && candidate->lastPosition() > start) {
                table = candidate;
            }
        }
    }
    if (table != nullptr) {
        MessageRange range;
        range.first = table->firstPosition();
        range.last = table->lastPosition();
        range.key = key;
        range.frame = table;
        message_ranges_.append(range);
    }
    hovered_id_ = 0;
}

void ChatView::clearConversation() {
    clear();
    message_ranges_.clear();
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
    return messageKeyAtPosition(cursorForPosition(viewport_pos).position(), key);
}

bool ChatView::messageKeyAtPosition(int position,
                                    chatbox::client::MessageKey* key) const {
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

void ChatView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F10 || event->key() == Qt::Key_Menu) {
        const QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) {
            chatbox::client::MessageKey key;
            if (messageKeyAtPosition(cursor.position(), &key)) {
                QRect anchor = messageAnchorRect(key.message_id);
                if (anchor.isNull()) {
                    anchor = cursorRect(cursor);
                }
                const QPoint global_pos = viewport()->mapToGlobal(anchor.center());
                emit messageMenuRequested(key.message_id, key.client_msg_id, global_pos);
                event->accept();
                return;
            }
        }
    }
    QTextBrowser::keyPressEvent(event);
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
    styleAnchorsInRange(0, doc->characterCount());
}

void ChatView::styleAnchorsInRange(int start, int end) {
    QTextDocument* doc = document();
    if (doc == nullptr || start >= end) {
        return;
    }
    // Collect first: merging formats invalidates block/fragment iterators.
    QVector<QPair<int, int>> ranges;
    for (QTextBlock block = doc->findBlock(qMax(0, start));
         block.isValid() && block.position() < end; block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.position() >= start &&
                fragment.position() < end && fragment.charFormat().isAnchor()) {
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
