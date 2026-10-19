#include "ui/user_search_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/identity.h"
#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

UserSearchDialog::UserSearchDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("搜索用户");
    resize(420, 420);

    auto* layout = new QVBoxLayout(this);
    auto* search_layout = new QHBoxLayout();
    search_edit_ = new QLineEdit(this);
    search_edit_->setPlaceholderText("输入用户名、昵称或用户ID");
    search_button_ = new QPushButton("搜索", this);
    search_layout->addWidget(search_edit_, 1);
    search_layout->addWidget(search_button_);
    layout->addLayout(search_layout);

    result_list_ = new QListWidget(this);
    result_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(result_list_, 1);

    auto* actions = new QHBoxLayout();
    actions->addStretch();
    profile_button_ = new QPushButton("查看资料", this);
    profile_button_->setEnabled(false);
    actions->addWidget(profile_button_);
    layout->addLayout(actions);

    status_label_ = new QLabel("输入用户名、昵称或用户ID后搜索", this);
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);

    connect(search_button_, &QPushButton::clicked, this, &UserSearchDialog::submitSearch);
    connect(search_edit_, &QLineEdit::returnPressed, this, &UserSearchDialog::submitSearch);
    connect(result_list_, &QListWidget::itemActivated, this,
            &UserSearchDialog::activateItem);
    connect(result_list_, &QListWidget::itemSelectionChanged, this,
            &UserSearchDialog::updateProfileButton);
    connect(profile_button_, &QPushButton::clicked, this,
            &UserSearchDialog::openSelectedProfile);
}

void UserSearchDialog::setResults(const QVector<UserSearchResult>& results) {
    result_list_->clear();
    for (const UserSearchResult& result : results) {
        QString text = result.nickname;
        if (!result.username.isEmpty()) {
            text += " (" + result.username + ")";
        }
        text += "  " + chatbox::client::publicUserId(result.user_id);
        if (result.online) {
            text += " ●";
        }
        auto* item = new QListWidgetItem(text, result_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(result.user_id));
        item->setData(Qt::UserRole + 1, result.username);
        item->setData(Qt::UserRole + 2, result.nickname);
    }
    if (!results.isEmpty()) {
        result_list_->setCurrentRow(0);
    }
    status_label_->setText(
        results.isEmpty()
            ? "没有找到用户"
            : QString("找到 %1 个用户，双击发送好友申请，或查看资料")
                  .arg(results.size()));
    updateProfileButton();
}

void UserSearchDialog::setBusy(bool busy) {
    search_button_->setEnabled(!busy);
    search_edit_->setEnabled(!busy);
}

void UserSearchDialog::setStatus(const QString& text, bool error) {
    status_label_->setText(text);
    status_label_->setStyleSheet(error
                                     ? QString("color: %1;").arg(theme::kSemanticDown)
                                     : QString("color: %1;").arg(theme::kBody));
}

void UserSearchDialog::clearResults() {
    result_list_->clear();
    status_label_->setText("输入用户名、昵称或用户ID后搜索");
    status_label_->setStyleSheet("color: " + QString::fromLatin1(theme::kBody) + ";");
    search_edit_->clear();
    setBusy(false);
    updateProfileButton();
}

void UserSearchDialog::submitSearch() {
    const QString keyword = search_edit_->text().trimmed();
    if (keyword.isEmpty()) {
        status_label_->setText("请输入用户名或昵称");
        return;
    }
    status_label_->setText("正在搜索...");
    emit searchRequested(keyword);
}

void UserSearchDialog::activateItem(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 user_id = item->data(Qt::UserRole).toULongLong();
    const QString username = item->data(Qt::UserRole + 1).toString();
    const QString nickname = item->data(Qt::UserRole + 2).toString();
    if (user_id != 0) {
        emit userActivated(user_id, username, nickname);
    }
}

void UserSearchDialog::openSelectedProfile() {
    QListWidgetItem* item = result_list_->currentItem();
    if (item == nullptr) {
        status_label_->setText("请先选择一位用户");
        return;
    }
    const quint64 user_id = item->data(Qt::UserRole).toULongLong();
    if (user_id != 0) {
        emit profileRequested(user_id);
    }
}

void UserSearchDialog::updateProfileButton() {
    QListWidgetItem* item = result_list_->currentItem();
    profile_button_->setEnabled(item != nullptr &&
                                item->data(Qt::UserRole).toULongLong() != 0);
}

QString UserSearchDialog::selectedDisplayName() const {
    QListWidgetItem* item = result_list_->currentItem();
    if (item == nullptr) {
        return QString();
    }
    return item->data(Qt::UserRole + 2).toString();
}
