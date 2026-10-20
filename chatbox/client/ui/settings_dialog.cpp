#include "ui/settings_dialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include "core/autostart.h"
#include "core/client_log.h"
#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

SettingsDialog::SettingsDialog(const QString& public_id, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("设置");
    resize(440, 410);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    id_label_ = new QLabel(public_id, this);
    id_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    id_label_->setStyleSheet("font-weight: 600; color: " +
                             QString::fromLatin1(theme::kInk) + ";");
    form->addRow("我的ID", id_label_);

    QSettings settings;
    host_edit_ = new QLineEdit(
        settings.value("server/host", "192.168.2.240").toString(), this);
    port_spin_ = new QSpinBox(this);
    port_spin_->setRange(1, 65535);
    port_spin_->setValue(settings.value("server/port", 9443).toInt());
    tls_check_ = new QCheckBox("TLS 加密连接", this);
    tls_check_->setChecked(settings.value("server/tls", true).toBool());
    notifications_check_ = new QCheckBox("消息提醒（任务栏闪烁/未读角标）", this);
    notifications_check_->setChecked(
        settings.value("settings/notifications", true).toBool());
    system_popup_check_ = new QCheckBox("系统弹窗通知", this);
    system_popup_check_->setChecked(
        settings.value("settings/system_popup", true).toBool());
    autostart_check_ = new QCheckBox("开机自启动（静默到托盘）", this);
    autostart_check_->setChecked(chatbox::client::isAutostartEnabled());

    form->addRow("服务器", host_edit_);
    form->addRow("端口", port_spin_);
    form->addRow("连接", tls_check_);
    form->addRow("通知", notifications_check_);
    form->addRow("", system_popup_check_);
    form->addRow("启动", autostart_check_);
    layout->addLayout(form);

    auto* log_layout = new QHBoxLayout();
    log_label_ = new QLabel(clientLogFilePath(), this);
    log_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* open_logs = new QPushButton("打开日志目录", this);
    log_layout->addWidget(log_label_, 1);
    log_layout->addWidget(open_logs);
    layout->addLayout(log_layout);

    auto* version = new QLabel(
        QString("ChatBox %1").arg(QCoreApplication::applicationVersion()), this);
    version->setStyleSheet("color: " + QString::fromLatin1(theme::kMuted) + ";");
    layout->addWidget(version);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(open_logs, &QPushButton::clicked, this, &SettingsDialog::openLogDirectory);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveSettings);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::saveSettings() {
    const QString host = host_edit_->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, "设置", "服务器地址不能为空");
        return;
    }
    quint16 port = static_cast<quint16>(port_spin_->value());
    const bool tls = tls_check_->isChecked();
    if (tls && port == 9100) {
        port = 9443;
    } else if (!tls && port == 9443) {
        port = 9100;
    }
    emit settingsSaved(host, port, tls, notifications_check_->isChecked(),
                       system_popup_check_->isChecked(),
                       autostart_check_->isChecked());
    accept();
}

void SettingsDialog::openLogDirectory() {
    const QString directory = QFileInfo(clientLogFilePath()).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}
