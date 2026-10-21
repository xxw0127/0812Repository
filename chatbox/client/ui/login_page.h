#ifndef CHATBOX_CLIENT_UI_LOGIN_PAGE_H
#define CHATBOX_CLIENT_UI_LOGIN_PAGE_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

class LoginPage : public QWidget {
    Q_OBJECT

public:
    explicit LoginPage(QWidget* parent = nullptr);

    void setStatus(const QString& text, bool error = false);
    void setBusy(bool busy);
    void clearPassword();
    void setServerSettings(const QString& host, quint16 port, bool tls);
    // 本地记录入口：只有本机确实存过该账号的聊天记录时才显示。
    void setLocalHistoryAvailable(bool available, const QString& nickname = QString());

signals:
    void loginRequested(const QString& host, quint16 port, const QString& username,
                        const QString& password, bool tls);
    void registerRequested(const QString& host, quint16 port, const QString& username,
                           const QString& password, const QString& nickname, bool tls);
    void cancelRequested();
    // 用户点击「查看本地记录」：请求以只读方式打开本机保存的聊天记录。
    void viewLocalHistoryRequested();

private slots:
    void submitLogin();
    void submitRegister();

private:
    QLineEdit* host_edit_ = nullptr;
    QSpinBox* port_spin_ = nullptr;
    QLineEdit* username_edit_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QLineEdit* nickname_edit_ = nullptr;
    QCheckBox* tls_check_ = nullptr;
    QWidget* advanced_panel_ = nullptr;
    QToolButton* advanced_toggle_ = nullptr;
    QPushButton* login_button_ = nullptr;
    QPushButton* register_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QPushButton* local_history_button_ = nullptr;
    QLabel* status_label_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_LOGIN_PAGE_H
