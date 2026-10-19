#include "ui/friend_list_widget.h"

#include <QMouseEvent>

namespace {

bool isAvatarZone(const QListWidget* list, QListWidgetItem* item,
                  const QPoint& position) {
    if (list == nullptr || item == nullptr ||
        item->data(Qt::UserRole).toULongLong() == 0) {
        return false;
    }
    const QRect rect = list->visualItemRect(item);
    const int x = position.x();
    // The delegate draws a 36 px avatar starting at row.left() + 12.
    // Treat the surrounding avatar column as the click target.
    return x >= rect.left() + 4 && x <= rect.left() + 58;
}

}  // namespace

FriendListWidget::FriendListWidget(QWidget* parent) : QListWidget(parent) {}

void FriendListWidget::mousePressEvent(QMouseEvent* event) {
    if (event != nullptr && event->button() == Qt::LeftButton) {
        QListWidgetItem* item = itemAt(event->pos());
        if (isAvatarZone(this, item, event->pos())) {
            // Suppress QListWidget's press-time selection so an avatar press
            // cannot switch the active chat before the matching release.
            avatar_press_active_ = true;
            avatar_press_user_id_ = item->data(Qt::UserRole).toULongLong();
            event->accept();
            return;
        }
    }
    avatar_press_active_ = false;
    avatar_press_user_id_ = 0;
    QListWidget::mousePressEvent(event);
}

void FriendListWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event != nullptr && event->button() == Qt::LeftButton &&
        avatar_press_active_) {
        const quint64 pressed_user_id = avatar_press_user_id_;
        avatar_press_active_ = false;
        avatar_press_user_id_ = 0;
        QListWidgetItem* item = itemAt(event->pos());
        if (item != nullptr &&
            item->data(Qt::UserRole).toULongLong() == pressed_user_id &&
            isAvatarZone(this, item, event->pos())) {
            emit avatarClicked(item);
        }
        // A mismatched release is discarded: the press never reached the base
        // class, so selecting or opening a different row would be wrong.
        event->accept();
        return;
    }
    QListWidget::mouseReleaseEvent(event);
}
