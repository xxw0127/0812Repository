#ifndef CHATROOM_CLIENT_UI_LOGIN_PAGE_H
#define CHATROOM_CLIENT_UI_LOGIN_PAGE_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class LoginPage : public QWidget {
    Q_OBJECT

public:
    explicit LoginPage(QWidget* parent = nullptr);

    void setStatus(const QString& text, bool error = false);
    void setBusy(bool busy);
    void clearPassword();
    void setServerSettings(const QString& host, quint16 port, bool tls);

signals:
    void loginRequested(const QString& host, quint16 port, const QString& username,
                        const QString& password, bool tls);
    void registerRequested(const QString& host, quint16 port, const QString& username,
                           const QString& password, const QString& nickname, bool tls);
    void cancelRequested();

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
    QPushButton* login_button_ = nullptr;
    QPushButton* register_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QLabel* status_label_ = nullptr;
};

#endif  // CHATROOM_CLIENT_UI_LOGIN_PAGE_H
