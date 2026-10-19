#ifndef CHATROOM_CLIENT_UI_PROFILE_DIALOG_H
#define CHATROOM_CLIENT_UI_PROFILE_DIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QImage>
#include <QString>

#include "core/user_profile.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QWidget;

// Shows another user's public profile or edits the current user's profile.
// The dialog never talks to the network or database: it only emits signals
// for MainWindow to handle.
class ProfileDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProfileDialog(QWidget* parent = nullptr);

    void setProfile(const chatroom::client::UserProfile& profile, bool self);
    void setAvatar(const QImage& image);
    void setBusy(bool busy);
    void setStatus(const QString& text, bool error = false);

signals:
    void saveRequested(const QString& nickname, const QString& signature,
                       int avatar_action, const QByteArray& avatar_jpeg);
    void messageRequested(quint64 user_id);
    void addFriendRequested(quint64 user_id);
    void removeFriendRequested(quint64 user_id);
    void remarkRequested(quint64 user_id);
    void refreshRequested(quint64 user_id);
    void friendRequestsRequested();

private slots:
    void chooseAvatar();
    void removeAvatar();
    void saveClicked();

private:
    void buildUi();
    void updateMode();
    void updateDisplay();
    void updateAvatarPreview();
    void updateRelationshipActions();
    void updateEnabledState();
    void updateCounters();

    QString currentDisplayName() const;
    QString currentSignature() const;
    QImage currentAvatarSource() const;

    chatroom::client::UserProfile profile_;
    bool self_ = false;
    bool busy_ = false;

    // 0 = unchanged, 1 = set, 2 = clear. Pending values are kept until a
    // profile/avatar update replaces them, so a failed save can be retried.
    int pending_avatar_action_ = 0;
    QByteArray pending_avatar_jpeg_;
    QImage pending_avatar_image_;
    QImage avatar_image_;

    QLabel* avatar_label_ = nullptr;
    QLabel* name_label_ = nullptr;
    QLabel* secondary_nickname_label_ = nullptr;
    QLabel* username_label_ = nullptr;
    QLabel* uid_label_ = nullptr;
    QLabel* online_label_ = nullptr;
    QLabel* signature_label_ = nullptr;
    QLabel* status_label_ = nullptr;

    QWidget* edit_panel_ = nullptr;
    QLineEdit* nickname_edit_ = nullptr;
    QPlainTextEdit* signature_edit_ = nullptr;
    QLabel* nickname_count_label_ = nullptr;
    QLabel* signature_count_label_ = nullptr;
    QWidget* self_avatar_row_ = nullptr;
    QPushButton* change_avatar_button_ = nullptr;
    QPushButton* remove_avatar_button_ = nullptr;

    QWidget* action_row_ = nullptr;
    QPushButton* message_button_ = nullptr;
    QPushButton* add_friend_button_ = nullptr;
    QPushButton* remark_button_ = nullptr;
    QPushButton* remove_friend_button_ = nullptr;
    QPushButton* pending_button_ = nullptr;

    QPushButton* refresh_button_ = nullptr;
    QPushButton* save_button_ = nullptr;
    QPushButton* close_button_ = nullptr;
};

#endif  // CHATROOM_CLIENT_UI_PROFILE_DIALOG_H
