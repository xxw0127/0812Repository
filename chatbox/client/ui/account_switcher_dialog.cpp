#include "ui/account_switcher_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/account_store.h"
#include "core/avatar_utils.h"
#include "core/credential_store.h"
#include "core/ui_theme.h"

namespace {

constexpr int kAvatarSize = 36;
constexpr int kUsernameRole = Qt::UserRole;

QPixmap avatarFor(const chatbox::client::AccountRecord& record) {
    if (!record.avatar_path.isEmpty()) {
        QPixmap cached(record.avatar_path);
        if (!cached.isNull()) {
            // The cached PNG is already square; mask it so it matches the
            // fallback circle instead of showing square corners.
            return QPixmap::fromImage(chatbox::client::circularAvatar(
                cached.toImage(), kAvatarSize));
        }
    }
    const QString label = record.nickname.isEmpty() ? record.username
                                                    : record.nickname;
    return QPixmap::fromImage(chatbox::client::fallbackAvatar(
        label, record.user_id, kAvatarSize));
}

}  // namespace

AccountSwitcherDialog::AccountSwitcherDialog(const QString& current_username,
                                             QWidget* parent)
    : QDialog(parent), current_username_(current_username) {
    setWindowTitle(QStringLiteral("切换账号"));
    setMinimumWidth(360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto* intro = new QLabel(QStringLiteral("选择要切换到的账号："), this);
    layout->addWidget(intro);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("AccountList"));
    list_->setIconSize(QSize(kAvatarSize, kAvatarSize));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAccessibleName(QStringLiteral("已保存的账号"));
    layout->addWidget(list_, 1);

    auto* hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("MutedLabel"));
    hint->setWordWrap(true);
    hint->setText(QStringLiteral("切换会退出当前账号并重新登录所选账号。"));
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto* switch_button =
        buttons->addButton(QStringLiteral("切换"), QDialogButtonBox::AcceptRole);
    switch_button->setObjectName(QStringLiteral("PrimaryButton"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(switch_button, &QPushButton::clicked, this,
            &AccountSwitcherDialog::acceptSelection);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            &AccountSwitcherDialog::acceptSelection);

    buildList();
}

void AccountSwitcherDialog::buildList() {
    list_->clear();
    const QVector<chatbox::client::AccountRecord> records =
        chatbox::client::loadAccounts();
    if (records.isEmpty()) {
        auto* empty = new QListWidgetItem(
            QStringLiteral("还没有保存的账号，登录一次后会出现在这里"));
        empty->setFlags(Qt::NoItemFlags);
        list_->addItem(empty);
        return;
    }

    for (const chatbox::client::AccountRecord& record : records) {
        const bool is_current = record.username == current_username_;
        QString label = record.nickname.isEmpty() ? record.username
                                                  : record.nickname;
        if (is_current) {
            label += QStringLiteral("（当前）");
        }
        auto* item = new QListWidgetItem(QIcon(avatarFor(record)), label);
        item->setData(kUsernameRole, record.username);

        QString tip = record.username;
        QString probe;
        if (!chatbox::client::loadAccountCredentials(record.username, &probe)) {
            // No usable saved password: say so before the user commits.
            tip += QStringLiteral(" · 需要重新输入密码");
        }
        item->setToolTip(tip);
        list_->addItem(item);

        if (is_current) {
            list_->setCurrentItem(item);
        }
    }

    if (list_->currentItem() == nullptr && list_->count() > 0) {
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem* item = list_->item(i);
            if (item->data(kUsernameRole).toString() != current_username_ &&
                (item->flags() & Qt::ItemIsEnabled)) {
                list_->setCurrentItem(item);
                break;
            }
        }
    }
}

void AccountSwitcherDialog::acceptSelection() {
    QListWidgetItem* item = list_->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString username = item->data(kUsernameRole).toString();
    if (username.isEmpty()) {
        return;
    }
    selected_username_ = username;
    accept();
}

QString AccountSwitcherDialog::selectedUsername() const {
    return selected_username_;
}
