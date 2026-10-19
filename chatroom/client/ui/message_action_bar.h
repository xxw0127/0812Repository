#ifndef CHATROOM_CLIENT_UI_MESSAGE_ACTION_BAR_H
#define CHATROOM_CLIENT_UI_MESSAGE_ACTION_BAR_H

#include <QFrame>

class QPushButton;

// Small floating bar with per-message actions (回复/撤回) that is shown
// while the mouse hovers a message bubble.
class MessageActionBar : public QFrame {
    Q_OBJECT

public:
    explicit MessageActionBar(QWidget* parent = nullptr);

    // Configures which actions are available; unavailable ones are hidden.
    void setActions(bool can_reply, bool can_recall);

signals:
    void replyRequested();
    void recallRequested();
    // The pointer left the whole bar (including its buttons).
    void hoverLeft();

protected:
    void leaveEvent(QEvent* event) override;

private:
    QPushButton* reply_button_ = nullptr;
    QPushButton* recall_button_ = nullptr;
};

#endif  // CHATROOM_CLIENT_UI_MESSAGE_ACTION_BAR_H
