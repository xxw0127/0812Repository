#ifndef CHATBOX_CLIENT_UI_EMPTY_STATE_H
#define CHATBOX_CLIENT_UI_EMPTY_STATE_H

#include <QIcon>
#include <QWidget>

class QLabel;
class QPushButton;

// Reusable centered empty/onboarding state. The icon is tinted with the muted
// theme token so no new image assets or colors are required.
class EmptyState : public QWidget {
    Q_OBJECT

public:
    explicit EmptyState(QWidget* parent = nullptr);

    void setIcon(const QIcon& icon);
    void setTitle(const QString& text);
    void setDescription(const QString& text);
    void setAction(const QString& text);

signals:
    void actionTriggered();

private:
    QLabel* icon_label_ = nullptr;
    QLabel* title_label_ = nullptr;
    QLabel* description_label_ = nullptr;
    QPushButton* action_button_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_EMPTY_STATE_H
