#include "ui/message_input.h"

#include <QKeyEvent>
#include <QMimeData>
#include <QTextDocument>
#include <QTextDocumentFragment>

MessageInput::MessageInput(QWidget* parent) : QTextEdit(parent) {
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
    const int content_height = static_cast<int>(document()->size().height()) + 16;
    setFixedHeight(qBound(40, content_height, 120));
}
