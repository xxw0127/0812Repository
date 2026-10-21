#ifndef CHATBOX_CLIENT_UI_ACCOUNT_SWITCHER_DIALOG_H
#define CHATBOX_CLIENT_UI_ACCOUNT_SWITCHER_DIALOG_H

#include <QDialog>
#include <QString>

class QListWidget;

// Lists every account this machine remembers, with its avatar and nickname so
// the user recognises who they are switching to before committing. Only the
// username is returned; re-authentication is MainWindow's job.
class AccountSwitcherDialog : public QDialog {
    Q_OBJECT

public:
    explicit AccountSwitcherDialog(const QString& current_username,
                                   QWidget* parent = nullptr);

    QString selectedUsername() const;

private slots:
    void acceptSelection();

private:
    void buildList();

    QListWidget* list_ = nullptr;
    QString current_username_;
    QString selected_username_;
};

#endif  // CHATBOX_CLIENT_UI_ACCOUNT_SWITCHER_DIALOG_H
