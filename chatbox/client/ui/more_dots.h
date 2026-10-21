#ifndef CHATBOX_CLIENT_UI_MORE_DOTS_H
#define CHATBOX_CLIENT_UI_MORE_DOTS_H

#include <QColor>
#include <QIcon>

namespace chatbox::client {

// Three filled, evenly spaced dots for the compact "more" button.
QIcon moreDotsIcon(const QColor& color = QColor(10, 11, 13));

}  // namespace chatbox::client

#endif  // CHATBOX_CLIENT_UI_MORE_DOTS_H
