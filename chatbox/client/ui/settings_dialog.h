#ifndef CHATBOX_CLIENT_UI_SETTINGS_DIALOG_H
#define CHATBOX_CLIENT_UI_SETTINGS_DIALOG_H

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const QString& public_id, QWidget* parent = nullptr);

signals:
    void settingsSaved(const QString& host, quint16 port, bool tls, bool notifications,
                       bool system_popup, bool autostart);

private slots:
    void saveSettings();
    void openLogDirectory();

private:
    QLineEdit* host_edit_ = nullptr;
    QSpinBox* port_spin_ = nullptr;
    QCheckBox* tls_check_ = nullptr;
    QCheckBox* notifications_check_ = nullptr;
    QCheckBox* system_popup_check_ = nullptr;
    QCheckBox* autostart_check_ = nullptr;
    QLabel* log_label_ = nullptr;
    QLabel* id_label_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_SETTINGS_DIALOG_H
