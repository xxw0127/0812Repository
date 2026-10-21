#include "ui/message_input.h"

#include <QFrame>
#include <QKeyEvent>
#include <QMimeData>
#include <QTextDocument>
#include <QTextDocumentFragment>

MessageInput::MessageInput(QWidget* parent) : QTextEdit(parent) {
    // Drop the native QFrame chrome; all visible borders come from QSS and
    // must follow the focus/theme rules.
    setFrameShape(QFrame::NoFrame);
    setLineWidth(0);
    // The stylesheet supplies the visual padding; QTextEdit's default 4px
    // document margin would otherwise make the empty document taller than the
    // viewport and force a scrollbar handle onto the right edge.
    document()->setDocumentMargin(0);
    setAcceptRichText(false);
    setMinimumHeight(40);
    setMaximumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(this, &QTextEdit::textChanged, this, &MessageInput::adjustHeight);
    adjustHeight();
}

void MessageInput::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // Shift+Enter inserts a newline. Enter and Ctrl+Enter both send:
        // Ctrl+Enter arrives as the same Key_Return with ControlModifier, so
        // the only modifier that needs special handling is Shift.
        if (event->modifiers() & Qt::ShiftModifier) {
            QTextEdit::keyPressEvent(event);
            return;
        }
        emit submitRequested();
        event->accept();
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void MessageInput::insertFromMimeData(const QMimeData* source) {
    if (source == nullptr) {
        return;
    }
    if (source->hasText()) {
        insertPlainText(source->text());
    }
}

void MessageInput::adjustHeight() {
    // 8px top + 8px bottom stylesheet padding, 1px top/bottom border, plus
    // one spare pixel so the document never exactly equals the viewport
    // height (Qt shows a scrollbar in that case).
    const int chrome_height = 19;
    const int content_height =
        static_cast<int>(document()->size().height()) + chrome_height;
    setFixedHeight(qBound(40, content_height, 120));
}
