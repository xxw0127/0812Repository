#ifndef CHATROOM_CLIENT_UI_FRIEND_ITEM_DELEGATE_H
#define CHATROOM_CLIENT_UI_FRIEND_ITEM_DELEGATE_H

#include <QStyledItemDelegate>

class FriendItemDelegate : public QStyledItemDelegate {
public:
    // Item data role holding a circular QPixmap. When absent the delegate keeps
    // drawing the colored initial avatar.
    static constexpr int kAvatarRole = Qt::UserRole + 7;

    explicit FriendItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

#endif  // CHATROOM_CLIENT_UI_FRIEND_ITEM_DELEGATE_H
