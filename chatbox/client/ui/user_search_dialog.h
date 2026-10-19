#ifndef CHATBOX_CLIENT_UI_USER_SEARCH_DIALOG_H
#define CHATBOX_CLIENT_UI_USER_SEARCH_DIALOG_H

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

struct UserSearchResult {
    quint64 user_id = 0;
    QString username;
    QString nickname;
    bool online = false;
};

class UserSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit UserSearchDialog(QWidget* parent = nullptr);

    void setResults(const QVector<UserSearchResult>& results);
    void setBusy(bool busy);
    void setStatus(const QString& text, bool error = false);
    void clearResults();
    QString selectedDisplayName() const;

signals:
    void searchRequested(const QString& keyword);
    void userActivated(quint64 user_id, const QString& username, const QString& nickname);
    void profileRequested(quint64 user_id);

private slots:
    void submitSearch();
    void activateItem(QListWidgetItem* item);
    void openSelectedProfile();

private:
    void updateProfileButton();

    QLineEdit* search_edit_ = nullptr;
    QPushButton* search_button_ = nullptr;
    QPushButton* profile_button_ = nullptr;
    QListWidget* result_list_ = nullptr;
    QLabel* status_label_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_USER_SEARCH_DIALOG_H
