#include "ui/friend_request_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

#include "core/identity.h"
#include "core/message_time.h"

FriendRequestDialog::FriendRequestDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("好友申请");
    resize(420, 400);

    auto* layout = new QVBoxLayout(this);
    request_list_ = new QListWidget(this);
    layout->addWidget(request_list_, 1);

    auto* buttons = new QHBoxLayout();
    auto* refresh_button = new QPushButton("刷新", this);
    profile_button_ = new QPushButton("查看资料", this);
    profile_button_->setEnabled(false);
    accept_button_ = new QPushButton("接受", this);
    accept_button_->setObjectName("PrimaryButton");
    reject_button_ = new QPushButton("拒绝", this);
    buttons->addWidget(refresh_button);
    buttons->addWidget(profile_button_);
    buttons->addStretch();
    buttons->addWidget(reject_button_);
    buttons->addWidget(accept_button_);
    layout->addLayout(buttons);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("MutedLabel");
    layout->addWidget(status_label_);

    connect(refresh_button, &QPushButton::clicked, this,
            &FriendRequestDialog::refreshRequested);
    connect(profile_button_, &QPushButton::clicked, this,
            &FriendRequestDialog::openCurrentProfile);
    connect(accept_button_, &QPushButton::clicked, this,
            &FriendRequestDialog::acceptCurrent);
    connect(reject_button_, &QPushButton::clicked, this,
            &FriendRequestDialog::rejectCurrent);
    connect(request_list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { openCurrentProfile(); });
    connect(request_list_, &QListWidget::itemSelectionChanged, this,
            &FriendRequestDialog::updateProfileButton);
}

void FriendRequestDialog::setRequests(const QVector<FriendRequestInfo>& requests) {
    request_list_->clear();
    for (const FriendRequestInfo& request : requests) {
        QString text = request.nickname;
        if (!request.username.isEmpty()) {
            text += " (" + request.username + ")";
        }
        text += "  " + chatbox::client::publicUserId(request.user_id);
        if (!request.created_at.isEmpty()) {
            const QDateTime utc = chatbox::client::parseServerUtc(request.created_at);
            text += "  " + (utc.isValid()
                                ? chatbox::client::formatLocalDate(utc) + " " +
                                      chatbox::client::formatLocalTime(utc)
                                : request.created_at);
        }
        auto* item = new QListWidgetItem(text, request_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(request.user_id));
        item->setData(Qt::UserRole + 1, request.nickname);
    }
    status_label_->setText(requests.isEmpty()
                               ? "没有待处理的好友申请"
                               : QString("有 %1 条待处理申请").arg(requests.size()));
    const bool has_item = request_list_->count() > 0;
    if (has_item) {
        request_list_->setCurrentRow(0);
    }
    accept_button_->setEnabled(has_item);
    reject_button_->setEnabled(has_item);
    updateProfileButton();
}

quint64 FriendRequestDialog::currentUserId() const {
    QListWidgetItem* item = request_list_->currentItem();
    if (item == nullptr) {
        return 0;
    }
    return item->data(Qt::UserRole).toULongLong();
}

void FriendRequestDialog::acceptCurrent() {
    const quint64 user_id = currentUserId();
    if (user_id != 0) {
        emit respondRequested(user_id, true);
    }
}

void FriendRequestDialog::rejectCurrent() {
    const quint64 user_id = currentUserId();
    if (user_id != 0) {
        emit respondRequested(user_id, false);
    }
}

void FriendRequestDialog::openCurrentProfile() {
    const quint64 user_id = currentUserId();
    if (user_id != 0) {
        emit profileRequested(user_id);
    }
}

void FriendRequestDialog::updateProfileButton() {
    profile_button_->setEnabled(currentUserId() != 0);
}

QString FriendRequestDialog::currentDisplayName() const {
    QListWidgetItem* item = request_list_->currentItem();
    if (item == nullptr) {
        return QString();
    }
    return item->data(Qt::UserRole + 1).toString();
}
