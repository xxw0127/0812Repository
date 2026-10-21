#include "core/focus_ring.h"

#include <QEvent>
#include <QFocusEvent>
#include <QStyle>
#include <QWidget>

namespace chatbox {
namespace client {

namespace {

void setKeyboardFocus(QWidget* widget, bool keyboard_focus) {
    if (widget->property("keyboardFocus").toBool() != keyboard_focus) {
        widget->setProperty("keyboardFocus", keyboard_focus);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

}  // namespace

bool isKeyboardFocusReason(Qt::FocusReason reason) {
    return reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
           reason == Qt::ShortcutFocusReason;
}

FocusRingFilter::FocusRingFilter(QObject* parent) : QObject(parent) {
}

bool FocusRingFilter::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr) {
        if (event->type() == QEvent::FocusIn) {
            auto* focus_event = static_cast<QFocusEvent*>(event);
            setKeyboardFocus(widget,
                             isKeyboardFocusReason(focus_event->reason()));
        } else if (event->type() == QEvent::FocusOut) {
            setKeyboardFocus(widget, false);
        } else if (event->type() == QEvent::MouseButtonPress) {
            // Clicking a widget that already owns keyboard focus must drop the
            // ring too; no FocusIn event is generated in that case. A list's
            // click arrives on its viewport, so clear the focused ancestor as
            // well.
            for (QWidget* current = widget; current != nullptr;
                 current = current->parentWidget()) {
                setKeyboardFocus(current, false);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace client
}  // namespace chatbox
