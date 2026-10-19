#ifndef CHATBOX_CLIENT_UI_FRIEND_REQUEST_DIALOG_H
#define CHATBOX_CLIENT_UI_FRIEND_REQUEST_DIALOG_H

#include <QDialog>
#include <QVector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

struct FriendRequestInfo {
    quint64 user_id = 0;
    QString username;
    QString nickname;
    QString created_at;
};

class FriendRequestDialog : public QDialog {
    Q_OBJECT

public:
    explicit FriendRequestDialog(QWidget* parent = nullptr);

    void setRequests(const QVector<FriendRequestInfo>& requests);
    QString currentDisplayName() const;

signals:
    void respondRequested(quint64 requester_id, bool accept);
    void refreshRequested();
    void profileRequested(quint64 user_id);

private slots:
    void acceptCurrent();
    void rejectCurrent();
    void openCurrentProfile();

private:
    quint64 currentUserId() const;
    void updateProfileButton();

    QListWidget* request_list_ = nullptr;
    QPushButton* profile_button_ = nullptr;
    QPushButton* accept_button_ = nullptr;
    QPushButton* reject_button_ = nullptr;
    QLabel* status_label_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_FRIEND_REQUEST_DIALOG_H
