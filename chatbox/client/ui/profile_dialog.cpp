#include "ui/profile_dialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "core/avatar_utils.h"
#include "core/identity.h"
#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

namespace {

const int kAvatarSize = 96;
const int kNicknameLimit = 32;
const int kSignatureLimit = 128;
const int kAvatarMaxBytes = 48 * 1024;

int codePointCount(const QString& text) {
    return text.toUcs4().size();
}

void setCounterText(QLabel* label, const QString& text, bool over_limit) {
    label->setText(text);
    label->setStyleSheet(over_limit
                             ? QString("color: %1; font-size: 11px;").arg(theme::kSemanticDown)
                             : QString("color: %1; font-size: 11px;").arg(theme::kMuted));
}

}  // namespace

ProfileDialog::ProfileDialog(QWidget* parent) : QDialog(parent) {
    buildUi();
    setProfile(chatbox::client::UserProfile(), true);
}

void ProfileDialog::buildUi() {
    setWindowTitle("个人资料");
    resize(460, 560);
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(12);

    avatar_label_ = new QLabel(this);
    avatar_label_->setObjectName("ProfileAvatar");
    avatar_label_->setFixedSize(kAvatarSize, kAvatarSize);
    avatar_label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(avatar_label_, 0, Qt::AlignHCenter);

    name_label_ = new QLabel(this);
    name_label_->setObjectName("ProfileNameLabel");
    name_label_->setAlignment(Qt::AlignCenter);
    name_label_->setWordWrap(true);
    layout->addWidget(name_label_);

    secondary_nickname_label_ = new QLabel(this);
    secondary_nickname_label_->setObjectName("ProfileNicknameLabel");
    secondary_nickname_label_->setAlignment(Qt::AlignCenter);
    secondary_nickname_label_->setWordWrap(true);
    secondary_nickname_label_->setStyleSheet("color: " +
                                             QString::fromLatin1(theme::kMuted) +
                                             "; font-size: 13px;");
    layout->addWidget(secondary_nickname_label_);

    username_label_ = new QLabel(this);
    username_label_->setObjectName("ProfileUsernameLabel");
    username_label_->setAlignment(Qt::AlignCenter);
    username_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    username_label_->setStyleSheet("color: " + QString::fromLatin1(theme::kBody) + ";");
    layout->addWidget(username_label_);

    uid_label_ = new QLabel(this);
    uid_label_->setObjectName("ProfileUidLabel");
    uid_label_->setAlignment(Qt::AlignCenter);
    uid_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    uid_label_->setStyleSheet("color: " + QString::fromLatin1(theme::kMuted) +
                              "; font-size: 12px;");
    layout->addWidget(uid_label_);

    online_label_ = new QLabel(this);
    online_label_->setObjectName("ProfileOnlineLabel");
    online_label_->setAlignment(Qt::AlignCenter);
    online_label_->setStyleSheet("font-size: 12px;");
    layout->addWidget(online_label_);

    signature_label_ = new QLabel(this);
    signature_label_->setObjectName("ProfileSignatureLabel");
    signature_label_->setAlignment(Qt::AlignCenter);
    signature_label_->setWordWrap(true);
    signature_label_->setStyleSheet("color: " + QString::fromLatin1(theme::kBody) +
                                    "; padding: 2px 8px;");
    layout->addWidget(signature_label_);

    self_avatar_row_ = new QWidget(this);
    self_avatar_row_->setObjectName("ProfileSelfAvatarRow");
    auto* avatar_buttons = new QHBoxLayout(self_avatar_row_);
    avatar_buttons->setContentsMargins(0, 0, 0, 0);
    avatar_buttons->setSpacing(8);
    change_avatar_button_ = new QPushButton("更换头像", self_avatar_row_);
    change_avatar_button_->setObjectName("ProfileChangeAvatarButton");
    remove_avatar_button_ = new QPushButton("移除头像", self_avatar_row_);
    remove_avatar_button_->setObjectName("ProfileRemoveAvatarButton");
    avatar_buttons->addStretch();
    avatar_buttons->addWidget(change_avatar_button_);
    avatar_buttons->addWidget(remove_avatar_button_);
    avatar_buttons->addStretch();
    layout->addWidget(self_avatar_row_);

    edit_panel_ = new QWidget(this);
    edit_panel_->setObjectName("ProfileEditPanel");
    auto* form = new QFormLayout(edit_panel_);
    form->setContentsMargins(0, 4, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* nickname_caption = new QLabel("昵称", edit_panel_);
    nickname_caption->setObjectName("MutedLabel");
    nickname_edit_ = new QLineEdit(edit_panel_);
    nickname_edit_->setObjectName("ProfileNicknameEdit");
    nickname_edit_->setPlaceholderText("昵称（1-32 个字符）");
    form->addRow(nickname_caption, nickname_edit_);

    nickname_count_label_ = new QLabel(edit_panel_);
    nickname_count_label_->setObjectName("ProfileNicknameCountLabel");
    nickname_count_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addRow(QString(), nickname_count_label_);

    auto* signature_caption = new QLabel("个性签名", edit_panel_);
    signature_caption->setObjectName("MutedLabel");
    signature_edit_ = new QPlainTextEdit(edit_panel_);
    signature_edit_->setObjectName("ProfileSignatureEdit");
    signature_edit_->setPlaceholderText("说点什么吧（0-128 个字符）");
    signature_edit_->setFixedHeight(86);
    signature_edit_->setTabChangesFocus(true);
    signature_edit_->setStyleSheet(
        "QPlainTextEdit { background: " + QString::fromLatin1(theme::kSurfaceStrong) +
        "; border: 1px solid " + QString::fromLatin1(theme::kHairline) +
        "; border-radius: 8px; padding: 8px 10px; }"
        "QPlainTextEdit:focus { background: " + QString::fromLatin1(theme::kCanvas) +
        "; border: 1px solid " + QString::fromLatin1(theme::kAccent) + "; }");
    form->addRow(signature_caption, signature_edit_);

    signature_count_label_ = new QLabel(edit_panel_);
    signature_count_label_->setObjectName("ProfileSignatureCountLabel");
    signature_count_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addRow(QString(), signature_count_label_);
    layout->addWidget(edit_panel_);

    action_row_ = new QWidget(this);
    action_row_->setObjectName("ProfileActionRow");
    auto* actions = new QHBoxLayout(action_row_);
    actions->setContentsMargins(0, 4, 0, 0);
    actions->setSpacing(8);
    message_button_ = new QPushButton("发消息", action_row_);
    message_button_->setObjectName("ProfileMessageButton");
    message_button_->setStyleSheet(
        "QPushButton { background: " + QString::fromLatin1(theme::kAccent) +
        "; color: " + QString::fromLatin1(theme::kOnPrimary) + "; font-weight: 600; }"
        "QPushButton:hover { background: " + QString::fromLatin1(theme::kAccentHover) + "; }"
        "QPushButton:pressed { background: " + QString::fromLatin1(theme::kAccentActive) + "; }"
        "QPushButton:disabled { color: " + QString::fromLatin1(theme::kMutedSoft) +
        "; background: " + QString::fromLatin1(theme::kSurfaceSoft) + "; }");
    remark_button_ = new QPushButton("设置备注", action_row_);
    remark_button_->setObjectName("ProfileRemarkButton");
    remove_friend_button_ = new QPushButton("删除好友", action_row_);
    remove_friend_button_->setObjectName("ProfileRemoveFriendButton");
    remove_friend_button_->setStyleSheet(
        "color: " + QString::fromLatin1(theme::kDangerText) + ";");
    add_friend_button_ = new QPushButton("添加好友", action_row_);
    add_friend_button_->setObjectName("ProfileAddFriendButton");
    add_friend_button_->setStyleSheet(message_button_->styleSheet());
    pending_button_ = new QPushButton("申请已发送", action_row_);
    pending_button_->setObjectName("ProfilePendingButton");
    pending_button_->setEnabled(false);
    actions->addStretch();
    actions->addWidget(message_button_);
    actions->addWidget(add_friend_button_);
    actions->addWidget(remark_button_);
    actions->addWidget(remove_friend_button_);
    actions->addWidget(pending_button_);
    actions->addStretch();
    layout->addWidget(action_row_);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("ProfileStatusLabel");
    status_label_->setWordWrap(true);
    status_label_->setStyleSheet("color: " + QString::fromLatin1(theme::kBody) + ";");
    layout->addWidget(status_label_);
    layout->addStretch(1);

    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(8);
    refresh_button_ = new QPushButton("刷新", this);
    refresh_button_->setObjectName("ProfileRefreshButton");
    save_button_ = new QPushButton("保存", this);
    save_button_->setObjectName("ProfileSaveButton");
    save_button_->setStyleSheet(
        "QPushButton { background: " + QString::fromLatin1(theme::kAccent) +
        "; color: " + QString::fromLatin1(theme::kOnPrimary) +
        "; font-weight: 600; min-width: 72px; }"
        "QPushButton:hover { background: " + QString::fromLatin1(theme::kAccentHover) + "; }"
        "QPushButton:pressed { background: " + QString::fromLatin1(theme::kAccentActive) + "; }"
        "QPushButton:disabled { color: " + QString::fromLatin1(theme::kMutedSoft) +
        "; background: " + QString::fromLatin1(theme::kSurfaceSoft) + "; }");
    close_button_ = new QPushButton("关闭", this);
    close_button_->setObjectName("ProfileCloseButton");
    bottom->addWidget(refresh_button_);
    bottom->addStretch();
    bottom->addWidget(save_button_);
    bottom->addWidget(close_button_);
    layout->addLayout(bottom);

    connect(change_avatar_button_, &QPushButton::clicked, this,
            &ProfileDialog::chooseAvatar);
    connect(remove_avatar_button_, &QPushButton::clicked, this,
            &ProfileDialog::removeAvatar);
    connect(save_button_, &QPushButton::clicked, this, &ProfileDialog::saveClicked);
    connect(close_button_, &QPushButton::clicked, this, &QDialog::reject);
    connect(message_button_, &QPushButton::clicked, this, [this]() {
        if (!busy_ && profile_.user_id != 0) {
            emit messageRequested(profile_.user_id);
        }
    });
    connect(add_friend_button_, &QPushButton::clicked, this, [this]() {
        if (!busy_ && profile_.user_id != 0) {
            emit addFriendRequested(profile_.user_id);
        }
    });
    connect(remark_button_, &QPushButton::clicked, this, [this]() {
        if (!busy_ && profile_.user_id != 0) {
            emit remarkRequested(profile_.user_id);
        }
    });
    connect(remove_friend_button_, &QPushButton::clicked, this, [this]() {
        if (!busy_ && profile_.user_id != 0) {
            emit removeFriendRequested(profile_.user_id);
        }
    });
    connect(refresh_button_, &QPushButton::clicked, this, [this]() {
        if (!busy_ && profile_.user_id != 0) {
            emit refreshRequested(profile_.user_id);
        }
    });
    connect(pending_button_, &QPushButton::clicked, this, [this]() {
        if (!busy_ && !self_ && profile_.relationship == 4) {
            emit friendRequestsRequested();
        }
    });
    connect(nickname_edit_, &QLineEdit::textChanged, this, [this]() {
        updateDisplay();
        updateCounters();
        updateAvatarPreview();
    });
    connect(signature_edit_, &QPlainTextEdit::textChanged, this, [this]() {
        updateDisplay();
        updateCounters();
    });
}

void ProfileDialog::setProfile(const chatbox::client::UserProfile& profile,
                               bool self) {
    const bool new_user = profile_.user_id != profile.user_id;
    const bool avatar_version_changed =
        profile_.avatar_version != profile.avatar_version;
    profile_ = profile;
    self_ = self;
    pending_avatar_action_ = 0;
    pending_avatar_jpeg_.clear();
    pending_avatar_image_ = QImage();
    if (!profile.avatar.isNull()) {
        avatar_image_ = profile.avatar;
    } else if (new_user || avatar_version_changed) {
        // A changed version without bytes means the avatar was cleared or is
        // still being fetched; do not keep showing a stale image.
        avatar_image_ = QImage();
    }

    {
        const QSignalBlocker nickname_blocker(nickname_edit_);
        const QSignalBlocker signature_blocker(signature_edit_);
        nickname_edit_->setText(profile_.nickname);
        signature_edit_->setPlainText(profile_.signature);
    }

    setWindowTitle(self_ ? "我的资料" : "个人资料");
    setStatus(QString());
    updateMode();
    updateDisplay();
    updateCounters();
    updateAvatarPreview();
}

void ProfileDialog::setAvatar(const QImage& image) {
    avatar_image_ = image;
    if (pending_avatar_action_ == 0) {
        updateAvatarPreview();
    }
    updateEnabledState();
}

void ProfileDialog::setBusy(bool busy) {
    busy_ = busy;
    updateEnabledState();
}

void ProfileDialog::setStatus(const QString& text, bool error) {
    status_label_->setText(text);
    status_label_->setStyleSheet(error
                                     ? QString("color: %1;").arg(theme::kSemanticDown)
                                     : QString("color: %1;").arg(theme::kBody));
}

void ProfileDialog::chooseAvatar() {
    if (busy_ || !self_) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, "选择头像", QString(), "图片 (*.png *.jpg *.jpeg)");
    qInfo("avatar file dialog result path=%s", qPrintable(path));
    if (path.isEmpty()) {
        return;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    // Qt 5.15 clears QImageReader::format() after read() for some plugins, so
    // capture the detected format (or let canRead() force content detection)
    // before decoding. Fall back to the suffix for formats without a plugin
    // name; the server still validates the file magic.
    QByteArray format = reader.format().toLower();
    if (format.isEmpty() && reader.canRead()) {
        format = reader.format().toLower();
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        setStatus(QString("无法读取图片：%1").arg(reader.errorString()), true);
        return;
    }
    if (format.isEmpty()) {
        format = QFileInfo(path).suffix().toLower().toLatin1();
    }

    qInfo("avatar file selected path=%s format=%s size=%dx%d",
          qPrintable(path), format.constData(), image.width(), image.height());
    if (format != "jpeg" && format != "jpg" && format != "png") {
        setStatus("仅支持 JPEG 或 PNG 图片", true);
        return;
    }

    // JPEG has no alpha channel; flatten transparency onto white before the
    // shared encoder instead of letting Qt turn it into black.
    QImage flattened(image.size(), QImage::Format_RGB32);
    flattened.fill(Qt::white);
    QPainter painter(&flattened);
    painter.drawImage(0, 0, image);
    painter.end();

    const QByteArray jpeg =
        chatbox::client::encodeAvatarJpeg(flattened, kAvatarMaxBytes);
    if (jpeg.isEmpty()) {
        setStatus("图片压缩后仍然过大", true);
        return;
    }

    pending_avatar_action_ = 1;
    pending_avatar_jpeg_ = jpeg;
    pending_avatar_image_ = flattened;
    setStatus("头像已选择，保存后生效");
    updateAvatarPreview();
    updateEnabledState();
}

void ProfileDialog::removeAvatar() {
    if (busy_ || !self_) {
        return;
    }
    pending_avatar_action_ = 2;
    pending_avatar_jpeg_.clear();
    pending_avatar_image_ = QImage();
    setStatus("头像将在保存后移除");
    updateAvatarPreview();
    updateEnabledState();
}

void ProfileDialog::saveClicked() {
    if (!self_ || busy_) {
        return;
    }

    const QString nickname = nickname_edit_->text().trimmed();
    const QString signature = signature_edit_->toPlainText().trimmed();
    const int nickname_count = codePointCount(nickname);
    const int signature_count = codePointCount(signature);

    if (nickname_count < 1) {
        setStatus("昵称不能为空", true);
        nickname_edit_->setFocus();
        return;
    }
    if (nickname_count > kNicknameLimit) {
        setStatus(QString("昵称最多 32 个字符（当前 %1 个）").arg(nickname_count),
                  true);
        nickname_edit_->setFocus();
        return;
    }
    if (signature_count > kSignatureLimit) {
        setStatus(QString("个性签名最多 128 个字符（当前 %1 个）")
                      .arg(signature_count),
                  true);
        signature_edit_->setFocus();
        return;
    }
    if (pending_avatar_action_ == 1 && pending_avatar_jpeg_.isEmpty()) {
        setStatus("头像数据无效，请重新选择", true);
        return;
    }

    setStatus("正在保存...");
    emit saveRequested(nickname, signature, pending_avatar_action_,
                       pending_avatar_action_ == 1 ? pending_avatar_jpeg_
                                                   : QByteArray());
}

void ProfileDialog::updateMode() {
    edit_panel_->setVisible(self_);
    self_avatar_row_->setVisible(self_);
    save_button_->setVisible(self_);
    refresh_button_->setVisible(!self_);
    action_row_->setVisible(!self_);
    updateRelationshipActions();
    updateEnabledState();
}

void ProfileDialog::updateDisplay() {
    QString display = currentDisplayName().trimmed();
    if (display.isEmpty()) {
        display = profile_.username.trimmed();
    }
    if (display.isEmpty()) {
        display = "未命名用户";
    }
    name_label_->setText(display);

    const QString username = profile_.username.trimmed();
    username_label_->setText(username.isEmpty() ? "@-" : "@" + username);
    uid_label_->setText(chatbox::client::publicUserId(profile_.user_id));

    const bool has_remark = !self_ && !profile_.remark.trimmed().isEmpty();
    secondary_nickname_label_->setText(
        has_remark ? QString("昵称  %1").arg(profile_.nickname) : QString());
    secondary_nickname_label_->setVisible(has_remark &&
                                          !profile_.nickname.trimmed().isEmpty());

    const QString signature = currentSignature().trimmed();
    signature_label_->setText(signature.isEmpty()
                                  ? "这个人很懒，什么都没写"
                                  : signature);

    const bool online = self_ || profile_.online;
    online_label_->setText(online ? "● 在线" : "● 离线");
    online_label_->setStyleSheet(
        online ? QString("font-size: 12px; color: %1;").arg(theme::kSemanticUpText)
               : QString("font-size: 12px; color: %1;").arg(theme::kMutedSoft));
}

void ProfileDialog::updateAvatarPreview() {
    const QImage source = currentAvatarSource();
    QString name = currentDisplayName().trimmed();
    if (name.isEmpty()) {
        name = profile_.username.trimmed();
    }
    const QImage image = source.isNull()
                             ? chatbox::client::fallbackAvatar(
                                   name, profile_.user_id, kAvatarSize)
                             : chatbox::client::circularAvatar(source, kAvatarSize);
    avatar_label_->setPixmap(QPixmap::fromImage(image));
}

void ProfileDialog::updateRelationshipActions() {
    message_button_->hide();
    add_friend_button_->hide();
    remark_button_->hide();
    remove_friend_button_->hide();
    pending_button_->hide();
    pending_button_->setToolTip(QString());

    if (self_) {
        return;
    }

    switch (profile_.relationship) {
        case 0:
            pending_button_->setText("这是你自己的资料");
            pending_button_->show();
            break;
        case 1:
            message_button_->show();
            remark_button_->show();
            remove_friend_button_->show();
            break;
        case 2:
            add_friend_button_->show();
            break;
        case 3:
            pending_button_->setText("申请已发送");
            pending_button_->show();
            break;
        case 4:
            pending_button_->setText("查看好友申请");
            pending_button_->show();
            break;
        default:
            pending_button_->setText("暂无可用操作");
            pending_button_->show();
            break;
    }
}

void ProfileDialog::updateEnabledState() {
    const bool ready = !busy_;
    nickname_edit_->setEnabled(ready && self_);
    signature_edit_->setEnabled(ready && self_);
    save_button_->setEnabled(ready && self_);
    change_avatar_button_->setEnabled(ready && self_);

    const bool has_avatar = pending_avatar_action_ == 1 ||
                            (pending_avatar_action_ != 2 && !avatar_image_.isNull());
    remove_avatar_button_->setEnabled(ready && self_ && has_avatar);

    message_button_->setEnabled(ready);
    add_friend_button_->setEnabled(ready);
    remark_button_->setEnabled(ready);
    remove_friend_button_->setEnabled(ready);
    pending_button_->setEnabled(ready && !self_ && profile_.relationship == 4);
    refresh_button_->setEnabled(ready && !self_ && profile_.user_id != 0);
    close_button_->setEnabled(true);
}

void ProfileDialog::updateCounters() {
    const int nickname_count = codePointCount(nickname_edit_->text().trimmed());
    const int signature_count =
        codePointCount(signature_edit_->toPlainText().trimmed());
    setCounterText(nickname_count_label_,
                   QString("%1/%2").arg(nickname_count).arg(kNicknameLimit),
                   nickname_count > kNicknameLimit);
    setCounterText(signature_count_label_,
                   QString("%1/%2").arg(signature_count).arg(kSignatureLimit),
                   signature_count > kSignatureLimit);
}

QString ProfileDialog::currentDisplayName() const {
    if (self_ && nickname_edit_ != nullptr) {
        const QString nickname = nickname_edit_->text().trimmed();
        if (!nickname.isEmpty()) {
            return nickname;
        }
        return profile_.nickname;
    }
    return chatbox::client::profileDisplayName(profile_);
}

QString ProfileDialog::currentSignature() const {
    if (self_ && signature_edit_ != nullptr) {
        return signature_edit_->toPlainText();
    }
    return profile_.signature;
}

QImage ProfileDialog::currentAvatarSource() const {
    if (pending_avatar_action_ == 1) {
        return pending_avatar_image_;
    }
    if (pending_avatar_action_ == 2) {
        return QImage();
    }
    if (!avatar_image_.isNull()) {
        return avatar_image_;
    }
    return profile_.avatar;
}
