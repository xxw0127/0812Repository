#ifndef CHATBOX_CLIENT_UI_FRIEND_LIST_WIDGET_H
#define CHATBOX_CLIENT_UI_FRIEND_LIST_WIDGET_H

#include <QListWidget>
#include <QPoint>

// Friend list that distinguishes clicks on a row's avatar area from clicks on
// the rest of the row. Avatar clicks emit avatarClicked() and are consumed so
// the normal item-click path (opening a private chat) is not triggered.
class FriendListWidget : public QListWidget {
    Q_OBJECT

public:
    explicit FriendListWidget(QWidget* parent = nullptr);

signals:
    void avatarClicked(QListWidgetItem* item);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool avatar_press_active_ = false;
    quint64 avatar_press_user_id_ = 0;
};

#endif  // CHATBOX_CLIENT_UI_FRIEND_LIST_WIDGET_H
