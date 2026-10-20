#include "ui/login_page.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

LoginPage::LoginPage(QWidget* parent) : QWidget(parent) {
    setObjectName("LoginPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->addStretch();

    auto* card = new QFrame(this);
    card->setObjectName("LoginCard");
    card->setFixedWidth(390);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(30, 28, 30, 24);
    card_layout->setSpacing(10);

    auto* logo = new QLabel("C", card);
    logo->setFixedSize(56, 56);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(
        "background: " + QString::fromLatin1(theme::kAccent) + "; color: " +
        QString::fromLatin1(theme::kOnPrimary) +
        "; border-radius: 28px; font-size: 28px; font-weight: 600;");
    card_layout->addWidget(logo, 0, Qt::AlignHCenter);

    auto* title = new QLabel("ChatBox", card);
    title->setObjectName("AppTitle");
    title->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(title);

    auto* subtitle = new QLabel("登录或创建账号", card);
    subtitle->setObjectName("AppSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(subtitle);
    card_layout->addSpacing(8);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    host_edit_ = new QLineEdit("192.168.2.240", card);
    port_spin_ = new QSpinBox(card);
    port_spin_->setRange(1, 65535);
    port_spin_->setValue(9443);
    tls_check_ = new QCheckBox("TLS 加密连接", card);
    tls_check_->setChecked(true);
    username_edit_ = new QLineEdit(card);
    username_edit_->setPlaceholderText("3-32 位字母、数字、下划线");
    password_edit_ = new QLineEdit(card);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setPlaceholderText("至少 8 位");
    nickname_edit_ = new QLineEdit(card);
    nickname_edit_->setPlaceholderText("注册时填写");

    QSettings settings;
    host_edit_->setText(settings.value("server/host", "192.168.2.240").toString());
    port_spin_->setValue(settings.value("server/port", 9443).toInt());
    tls_check_->setChecked(settings.value("server/tls", true).toBool());

    form->addRow("服务器", host_edit_);
    form->addRow("端口", port_spin_);
    form->addRow("连接", tls_check_);
    form->addRow("用户名", username_edit_);
    form->addRow("密码", password_edit_);
    form->addRow("昵称", nickname_edit_);
    card_layout->addLayout(form);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    login_button_ = new QPushButton("登录", card);
    login_button_->setObjectName("PrimaryButton");
    login_button_->setMinimumHeight(38);
    register_button_ = new QPushButton("注册", card);
    register_button_->setMinimumHeight(38);
    cancel_button_ = new QPushButton("取消", card);
    cancel_button_->hide();
    login_button_->setDefault(true);
    buttons->addWidget(login_button_, 1);
    buttons->addWidget(register_button_, 1);
    buttons->addWidget(cancel_button_);
    card_layout->addLayout(buttons);

    status_label_ = new QLabel(card);
    status_label_->setWordWrap(true);
    status_label_->setMinimumHeight(32);
    status_label_->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(status_label_);

    root->addWidget(card, 0, Qt::AlignCenter);
    root->addStretch();

    connect(login_button_, &QPushButton::clicked, this, &LoginPage::submitLogin);
    connect(register_button_, &QPushButton::clicked, this, &LoginPage::submitRegister);
    connect(cancel_button_, &QPushButton::clicked, this, &LoginPage::cancelRequested);
    connect(password_edit_, &QLineEdit::returnPressed, this, &LoginPage::submitLogin);
    connect(tls_check_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && port_spin_->value() == 9100) {
            port_spin_->setValue(9443);
        } else if (!checked && port_spin_->value() == 9443) {
            port_spin_->setValue(9100);
        }
    });
}

void LoginPage::setStatus(const QString& text, bool error) {
    status_label_->setText(text);
    status_label_->setStyleSheet(error
                                     ? QString("color: %1;").arg(theme::kSemanticDown)
                                     : QString("color: %1;").arg(theme::kBody));
}

void LoginPage::setBusy(bool busy) {
    login_button_->setDisabled(busy);
    register_button_->setDisabled(busy);
    cancel_button_->setVisible(busy);
}

void LoginPage::clearPassword() {
    password_edit_->clear();
}

void LoginPage::setServerSettings(const QString& host, quint16 port, bool tls) {
    QSignalBlocker blocker(tls_check_);
    host_edit_->setText(host);
    port_spin_->setValue(port);
    tls_check_->setChecked(tls);
}

void LoginPage::submitLogin() {
    const QString host = host_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();
    if (host.isEmpty() || username.isEmpty() || password.isEmpty()) {
        setStatus("请填写服务器、用户名和密码", true);
        return;
    }
    if (password.size() < 8) {
        setStatus("密码至少 8 位", true);
        return;
    }
    setStatus("正在连接并登录...");
    emit loginRequested(host, static_cast<quint16>(port_spin_->value()), username, password,
                        tls_check_->isChecked());
}

void LoginPage::submitRegister() {
    const QString host = host_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();
    const QString nickname = nickname_edit_->text().trimmed();
    if (host.isEmpty() || username.isEmpty() || password.isEmpty() || nickname.isEmpty()) {
        setStatus("注册需要服务器、用户名、密码和昵称", true);
        return;
    }
    if (password.size() < 8) {
        setStatus("密码至少 8 位", true);
        return;
    }
    if (nickname.toUcs4().size() > 32) {
        setStatus("昵称不能超过 32 个字符", true);
        return;
    }
    setStatus("正在注册...");
    emit registerRequested(host, static_cast<quint16>(port_spin_->value()), username, password,
                           nickname, tls_check_->isChecked());
}
