#include "ui/main_window.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSize>
#include <QPainter>
#include <QPixmap>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include "core/network_thread.h"
#include "core/avatar_utils.h"
#include "core/identity.h"
#include "core/message_renderer.h"
#include "core/message_time.h"
#include "core/protocol_qt.h"
#include "core/ui_theme.h"
#include "ui/chat_view.h"
#include "ui/login_page.h"
#include "ui/friend_item_delegate.h"
#include "ui/friend_list_widget.h"
#include "ui/friend_request_dialog.h"
#include "ui/message_action_bar.h"
#include "ui/message_search_dialog.h"
#include "ui/profile_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/user_search_dialog.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

namespace {

bool readString(chatbox::ByteReader& reader, QString& value) {
    std::string text;
    if (!reader.getString(text)) {
        return false;
    }
    value = chatbox::fromUtf8(text);
    return true;
}

// Fallback letter avatar for lists. The rendering (palette included) lives in
// core/avatar_utils.cpp: a private palette here made the same user show
// different colours in the lists and in the profile dialog.
QIcon avatarIcon(const QString& name, quint64 seed) {
    return QIcon(
        QPixmap::fromImage(chatbox::client::fallbackAvatar(name, seed, 36)));
}

QString errorText(quint16 code, const QString& message) {
    switch (code) {
        case 1001:
            return "请求参数错误";
        case 1002:
            return "会话已失效，请重新登录";
        case 1003:
            return "用户名已存在";
        case 1004:
            return "用户名或密码错误";
        case 1005:
            return "操作过于频繁，请稍后再试";
        case 1008:
            return "用户不存在";
        case 1009:
            return "会话已过期，请重新登录";
        case 1010:
            return "账号已在其他连接登录";
        case 1012:
            return "你们已经是好友";
        case 1013:
            return "好友申请已发送，请等待对方处理";
        case 1014:
            return "对方已向你发送好友申请，请先处理";
        case 1015:
            return "好友申请已被处理或不存在";
        case 1016:
            return "你们还不是好友";
        case 1017:
            return "头像格式不支持或超过 48 KiB";
        case 2001:
            return "服务器内部错误";
        default:
            return message;
    }
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ChatBox");
    resize(1120, 720);
    buildUi();
    QSettings initial_settings;
    notifications_enabled_ =
        initial_settings.value("settings/notifications", true).toBool();
    send_timeout_timer_ = new QTimer(this);
    send_timeout_timer_->setInterval(1000);
    connect(send_timeout_timer_, &QTimer::timeout, this, &MainWindow::onSendTimeout);
    send_timeout_timer_->start();
    auth_timeout_timer_ = new QTimer(this);
    auth_timeout_timer_->setSingleShot(true);
    auth_timeout_timer_->setInterval(8000);
    connect(auth_timeout_timer_, &QTimer::timeout, this, &MainWindow::onAuthTimeout);
    reconnect_timer_ = new QTimer(this);
    reconnect_timer_->setSingleShot(true);
    connect(reconnect_timer_, &QTimer::timeout, this, [this] {
        reconnect_scheduled_ = false;
        if (!shutting_down_ && logged_in_) {
            startConnection();
        }
    });
    search_timeout_timer_ = new QTimer(this);
    search_timeout_timer_->setSingleShot(true);
    search_timeout_timer_->setInterval(8000);
    connect(search_timeout_timer_, &QTimer::timeout, this, [this] {
        user_search_seq_ = 0;
        if (user_search_dialog_ != nullptr) {
            user_search_dialog_->setBusy(false);
            user_search_dialog_->setStatus("搜索超时，请重试", true);
        }
    });
    message_search_timeout_timer_ = new QTimer(this);
    message_search_timeout_timer_->setSingleShot(true);
    message_search_timeout_timer_->setInterval(8000);
    connect(message_search_timeout_timer_, &QTimer::timeout, this, [this] {
        message_search_seq_ = 0;
        if (message_search_dialog_ != nullptr) {
            message_search_dialog_->setBusy(false);
            message_search_dialog_->setStatus("搜索超时，请重试", true);
        }
    });
    profile_timeout_timer_ = new QTimer(this);
    profile_timeout_timer_->setSingleShot(true);
    profile_timeout_timer_->setInterval(8000);
    connect(profile_timeout_timer_, &QTimer::timeout, this, [this] {
        if (pending_profile_requests_.isEmpty()) {
            return;
        }
        pending_profile_requests_.clear();
        profile_request_seqs_.clear();
        pending_profile_seqs_.clear();
        if (profile_dialog_ != nullptr) {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus("加载资料超时，请重试", true);
        }
        setStatus("加载资料超时，请重试", true);
    });
    avatar_timeout_timer_ = new QTimer(this);
    avatar_timeout_timer_->setSingleShot(true);
    avatar_timeout_timer_->setInterval(8000);
    connect(avatar_timeout_timer_, &QTimer::timeout, this, [this] {
        if (pending_avatar_requests_.isEmpty()) {
            return;
        }
        qWarning("avatar requests timed out: %d pending",
                 static_cast<int>(pending_avatar_requests_.size()));
        pending_avatar_requests_.clear();
        avatar_request_seqs_.clear();
        pending_avatar_seqs_.clear();
        pending_avatar_versions_.clear();
    });
    connect(chat_view_, &QTextBrowser::anchorClicked, this,
            [this](const QUrl& url) {
                if (url.scheme() == "retry") {
                    retryMessage(url.path());
                }
            });
    connect(chat_view_, &ChatView::hoveredMessage, this,
            [this](quint64 message_id, const QRect& meta_rect) {
                showMessageActions(message_id, meta_rect);
            });
    connect(chat_view_, &ChatView::hoverCleared, this,
            &MainWindow::hideMessageActionsUnlessHovered);
    connect(chat_view_, &ChatView::messageMenuRequested, this,
            &MainWindow::showMessageMenu);
    connect(chat_view_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) {
                if (chat_view_ != nullptr) {
                    chat_view_->clearHoverState();
                }
                hideMessageActionBar();
            });
    if (message_action_bar_ != nullptr) {
        connect(message_action_bar_, &MessageActionBar::replyRequested, this, [this] {
            const chatbox::client::ChatMessage* message =
                message_store_.findMessage(action_bar_message_id_);
            if (message != nullptr) {
                setReplyTo(*message);
            }
            hideMessageActionBar();
        });
        connect(message_action_bar_, &MessageActionBar::recallRequested, this, [this] {
            const quint64 message_id = action_bar_message_id_;
            hideMessageActionBar();
            sendRecall(message_id);
        });
        connect(message_action_bar_, &MessageActionBar::hoverLeft, this, [this] {
            hideMessageActionBar();
            if (chat_view_ != nullptr) {
                chat_view_->clearHoverState();
            }
        });
    }
    loadSession();
    if (!logged_in_) {
        setStatus("请输入服务器和账号");
    }
}

MainWindow::~MainWindow() {
    shutting_down_ = true;
    // Destroy the dialog(s) while all MainWindow members are still alive.
    // Without this, a child dialog (including one already closed with a
    // pending deleteLater) can be deleted by QObject base teardown after the
    // derived members, including the dialog state, are gone.
    const auto profile_dialogs = findChildren<ProfileDialog*>();
    for (ProfileDialog* dialog : profile_dialogs) {
        dialog->disconnect(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        delete dialog;
    }
    profile_dialog_ = nullptr;
    profile_dialog_user_id_ = 0;
    profile_dialog_fallback_name_.clear();
    profile_dialog_profile_ = chatbox::client::UserProfile();
    if (network_ != nullptr) {
        network_->requestStop();
        network_->wait();
    }
}

void MainWindow::buildUi() {
    stack_ = new QStackedWidget(this);
    login_page_ = new LoginPage(stack_);
    chat_page_ = buildChatPage();
    stack_->addWidget(login_page_);
    stack_->addWidget(chat_page_);
    stack_->setCurrentWidget(login_page_);
    setCentralWidget(stack_);

    status_label_ = new QLabel(this);
    statusBar()->addWidget(status_label_, 1);

    connect(login_page_, &LoginPage::loginRequested, this, &MainWindow::onLoginRequested);
    connect(login_page_, &LoginPage::registerRequested, this,
            &MainWindow::onRegisterRequested);
    connect(login_page_, &LoginPage::cancelRequested, this, [this] {
        logging_out_ = true;
        logged_in_ = false;
        if (network_ != nullptr) {
            network_->requestStop();
        }
        clearSession();
        login_page_->setStatus("已取消登录");
        setStatus("已取消登录");
    });
}

QWidget* MainWindow::buildChatPage() {
    auto* page = new QWidget(this);
    page->setObjectName("ChatPage");
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* left_panel = new QWidget(page);
    left_panel->setObjectName("Sidebar");
    left_panel->setFixedWidth(300);
    auto* left = new QVBoxLayout(left_panel);
    left->setContentsMargins(12, 12, 12, 12);
    left->setSpacing(6);

    auto* search_users_button = new QPushButton("搜索用户", left_panel);
    search_users_button->setIcon(QIcon(":/icons/search.png"));
    search_users_button->setIconSize(QSize(16, 16));
    left->addWidget(search_users_button);

    auto* rooms_label = new QLabel("大厅", left_panel);
    rooms_label->setObjectName("SectionLabel");
    left->addWidget(rooms_label);
    room_list_ = new QListWidget(left_panel);
    room_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    room_list_->setFocusPolicy(Qt::StrongFocus);
    room_list_->setIconSize(QSize(30, 30));
    room_list_->setFixedHeight(58);
    left->addWidget(room_list_);

    friend_requests_button_ = new QPushButton("好友申请", left_panel);
    friend_requests_button_->setIcon(QIcon(":/icons/users.png"));
    friend_requests_button_->setIconSize(QSize(16, 16));
    left->addWidget(friend_requests_button_);

    auto* conversations_label = new QLabel("好友", left_panel);
    conversations_label->setObjectName("SectionLabel");
    left->addWidget(conversations_label);
    conversation_list_ = new FriendListWidget(left_panel);
    conversation_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    conversation_list_->setFocusPolicy(Qt::StrongFocus);
    conversation_list_->setIconSize(QSize(30, 30));
    conversation_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    conversation_list_->setItemDelegate(new FriendItemDelegate(conversation_list_));
    conversation_list_->setSpacing(0);
    left->addWidget(conversation_list_, 1);

    my_profile_button_ = new QPushButton(left_panel);
    my_profile_button_->setObjectName("MyProfileButton");
    my_profile_button_->setCursor(Qt::PointingHandCursor);
    my_profile_button_->setFixedHeight(52);
    my_profile_button_->setStyleSheet(
        "QPushButton#MyProfileButton {"
        " background: " + QString::fromLatin1(theme::kCanvas) +
        "; border: none; border-top: 1px solid " + QString::fromLatin1(theme::kHairline) +
        "; border-radius: 0; padding: 4px 2px; }"
        "QPushButton#MyProfileButton:hover { background: " +
        QString::fromLatin1(theme::kSurfaceSoft) +
        "; }"
        "QPushButton#MyProfileButton:pressed { background: " +
        QString::fromLatin1(theme::kSurfaceStrong) + "; }");
    auto* profile_layout = new QHBoxLayout(my_profile_button_);
    profile_layout->setContentsMargins(8, 2, 8, 2);
    profile_layout->setSpacing(10);
    my_profile_avatar_ = new QLabel(my_profile_button_);
    my_profile_avatar_->setFixedSize(32, 32);
    my_profile_avatar_->setAttribute(Qt::WA_TransparentForMouseEvents);
    profile_layout->addWidget(my_profile_avatar_);
    auto* profile_text_layout = new QVBoxLayout();
    profile_text_layout->setContentsMargins(0, 0, 0, 0);
    profile_text_layout->setSpacing(0);
    my_profile_name_ = new QLabel(my_profile_button_);
    my_profile_name_->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: " +
        QString::fromLatin1(theme::kInk) + ";");
    my_profile_name_->setAttribute(Qt::WA_TransparentForMouseEvents);
    my_profile_uid_ = new QLabel(my_profile_button_);
    my_profile_uid_->setStyleSheet("font-size: 11px; color: " +
                                   QString::fromLatin1(theme::kMuted) + ";");
    my_profile_uid_->setAttribute(Qt::WA_TransparentForMouseEvents);
    profile_text_layout->addWidget(my_profile_name_);
    profile_text_layout->addWidget(my_profile_uid_);
    profile_layout->addLayout(profile_text_layout, 1);
    left->addWidget(my_profile_button_);
    updateOwnProfileStrip();
    layout->addWidget(left_panel);

    auto* center_panel = new QWidget(page);
    center_panel->setObjectName("ChatPanel");
    auto* center = new QVBoxLayout(center_panel);
    center->setContentsMargins(0, 0, 0, 0);
    center->setSpacing(0);

    auto* header = new QWidget(center_panel);
    header->setObjectName("ChatHeader");
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(16, 10, 16, 8);
    header_layout->setSpacing(4);
    connection_banner_ = new QLabel(header);
    connection_banner_->setWordWrap(true);
    connection_banner_->hide();
    header_layout->addWidget(connection_banner_);
    auto* header_row = new QHBoxLayout();
    context_avatar_ = new QLabel(header);
    context_avatar_->setFixedSize(36, 36);
    context_avatar_->setPixmap(avatarIcon("C", 0).pixmap(32, 32));
    context_avatar_->setCursor(Qt::ArrowCursor);
    context_avatar_->setToolTip("查看资料");
    context_avatar_->installEventFilter(this);
    header_row->addWidget(context_avatar_);
    context_label_ = new QLabel("请选择房间或私聊", header);
    context_label_->setObjectName("ContextTitle");
    header_row->addWidget(context_label_, 1);
    more_button_ = new QToolButton(header);
    more_button_->setObjectName("MoreButton");
    more_button_->setText(QStringLiteral("⋮"));
    more_button_->setToolTip("更多");
    more_button_->setFixedSize(30, 30);
    more_button_->setPopupMode(QToolButton::InstantPopup);
    more_menu_ = new QMenu(this);
    more_menu_->addAction(QIcon(":/icons/message.png"), "消息搜索", this,
                          &MainWindow::openMessageSearch);
    load_more_action_ = more_menu_->addAction("加载更早消息", this,
                                              &MainWindow::onLoadMoreClicked);
    load_more_action_->setEnabled(false);
    more_button_->setMenu(more_menu_);
    header_row->addWidget(more_button_);
    header_layout->addLayout(header_row);
    center->addWidget(header);

    chat_view_ = new ChatView(center_panel);
    chat_view_->setReadOnly(true);
    chat_view_->setOpenExternalLinks(false);
    chat_view_->setOpenLinks(false);
    center->addWidget(chat_view_, 1);

    // Floating per-message actions; lives in the viewport so it can be
    // positioned in view coordinates and disappears as soon as the pointer
    // leaves the message or the view scrolls.
    message_action_bar_ = new MessageActionBar(chat_view_->viewport());
    message_action_bar_->hide();

    auto* composer = new QWidget(center_panel);
    composer->setObjectName("Composer");
    auto* composer_layout = new QVBoxLayout(composer);
    composer_layout->setContentsMargins(12, 8, 12, 10);
    composer_layout->setSpacing(6);

    reply_label_ = new QLabel(composer);
    reply_label_->setObjectName("MutedLabel");
    reply_label_->setWordWrap(true);
    reply_label_->hide();
    reply_cancel_button_ = new QPushButton("取消回复", composer);
    reply_cancel_button_->hide();
    auto* reply_layout = new QHBoxLayout();
    reply_layout->setContentsMargins(0, 0, 0, 0);
    reply_layout->addWidget(reply_label_, 1);
    reply_layout->addWidget(reply_cancel_button_);
    composer_layout->addLayout(reply_layout);

    auto* input_layout = new QHBoxLayout();
    input_layout->setSpacing(6);
    input_edit_ = new QLineEdit(composer);
    input_edit_->setObjectName("MessageInput");
    input_edit_->setPlaceholderText("输入消息，回车发送");
    mention_button_ = new QPushButton("@", composer);
    mention_button_->setObjectName("IconButton");
    mention_button_->setFixedSize(38, 38);
    mention_button_->setToolTip("提及成员");
    send_button_ = new QPushButton(QIcon(":/icons/send.png"), "", composer);
    send_button_->setObjectName("SendButton");
    send_button_->setIconSize(QSize(18, 18));
    send_button_->setFixedSize(38, 38);
    send_button_->setToolTip("发送");
    input_layout->addWidget(input_edit_, 1);
    input_layout->addWidget(mention_button_);
    input_layout->addWidget(send_button_);
    composer_layout->addLayout(input_layout);
    center->addWidget(composer);
    layout->addWidget(center_panel, 1);

    auto* right_panel = new QWidget(page);
    right_panel->setObjectName("RightSidebar");
    right_panel->setFixedWidth(220);
    auto* right = new QVBoxLayout(right_panel);
    right->setContentsMargins(12, 12, 12, 12);
    right->setSpacing(6);
    auto* settings_button = new QPushButton("设置", right_panel);
    settings_button->setIcon(QIcon(":/icons/settings.png"));
    settings_button->setIconSize(QSize(16, 16));
    right->addWidget(settings_button);
    logout_button_ = new QPushButton("退出登录", right_panel);
    logout_button_->setIcon(QIcon(":/icons/logout.png"));
    logout_button_->setIconSize(QSize(16, 16));
    right->addWidget(logout_button_);
    auto* members_label = new QLabel("在线成员", right_panel);
    members_label->setObjectName("SectionLabel");
    right->addWidget(members_label);
    member_list_ = new QListWidget(right_panel);
    member_list_->setIconSize(QSize(28, 28));
    member_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    right->addWidget(member_list_, 1);
    layout->addWidget(right_panel);

    connect(room_list_, &QListWidget::itemClicked, this, &MainWindow::onRoomClicked);
    connect(room_list_, &QListWidget::itemActivated, this, &MainWindow::onRoomClicked);
    connect(conversation_list_, &QListWidget::itemClicked, this,
            &MainWindow::onConversationClicked);
    connect(conversation_list_, &FriendListWidget::avatarClicked, this,
            &MainWindow::onFriendAvatarClicked);
    connect(conversation_list_, &QListWidget::itemActivated, this,
            &MainWindow::onConversationClicked);
    connect(conversation_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::onFriendContextMenu);
    connect(my_profile_button_, &QPushButton::clicked, this,
            &MainWindow::openOwnProfile);
    connect(friend_requests_button_, &QPushButton::clicked, this,
            &MainWindow::openFriendRequests);
    connect(member_list_, &QListWidget::itemDoubleClicked, this,
            &MainWindow::onMemberDoubleClicked);
    connect(member_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::onMemberContextMenu);
    connect(search_users_button, &QPushButton::clicked, this, &MainWindow::openUserSearch);
    connect(send_button_, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(input_edit_, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    connect(mention_button_, &QPushButton::clicked, this, &MainWindow::insertMention);
    connect(reply_cancel_button_, &QPushButton::clicked, this, &MainWindow::clearReply);
    connect(logout_button_, &QPushButton::clicked, this, &MainWindow::logout);
    connect(settings_button, &QPushButton::clicked, this, &MainWindow::openSettings);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_ = new QSystemTrayIcon(this);
        tray_->setIcon(QIcon(QStringLiteral(":/app.ico")));
        tray_->setToolTip("ChatBox");
        tray_->show();
    }

    return page;
}

void MainWindow::saveSession() {
    if (token_.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue("session/host", host_);
    settings.setValue("session/port", port_);
    settings.setValue("session/tls", use_tls_);
    settings.setValue("session/token", token_);
    settings.setValue("session/user_id", QVariant::fromValue<qulonglong>(user_id_));
    settings.setValue("session/nickname", nickname_);
    settings.setValue("server/host", host_);
    settings.setValue("server/port", port_);
    settings.setValue("server/tls", use_tls_);
    settings.sync();
}

void MainWindow::loadSession() {
    QSettings settings;
    const QString token = settings.value("session/token").toString();
    if (token.isEmpty()) {
        return;
    }
    host_ = settings.value("session/host", "192.168.2.113").toString();
    port_ = static_cast<quint16>(settings.value("session/port", 9443).toUInt());
    use_tls_ = settings.value("session/tls", true).toBool();
    token_ = token;
    user_id_ = settings.value("session/user_id").toULongLong();
    nickname_ = settings.value("session/nickname").toString();
    logged_in_ = true;
    login_page_->setBusy(true);
    login_page_->setStatus("正在恢复上次登录...");
    startConnection();
}

void MainWindow::logout() {
    if (!logged_in_) {
        clearSession();
        return;
    }
    if (logging_out_) {
        return;
    }
    logging_out_ = true;
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdLogout;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
    QTimer::singleShot(800, this, &MainWindow::finishLogout);
}

void MainWindow::finishLogout() {
    if (logout_finishing_) {
        return;
    }
    logout_finishing_ = true;
    logged_in_ = false;
    if (network_ != nullptr) {
        network_->requestStop();
    }
    clearSession();
    login_page_->setStatus("已退出登录");
    setStatus("已退出登录");
}

void MainWindow::openSettings() {
    SettingsDialog dialog(chatbox::client::publicUserId(user_id_), this);
    connect(&dialog, &SettingsDialog::settingsSaved, this,
            [this](const QString& host, quint16 port, bool tls,
                   bool notifications) {
                QSettings settings;
                settings.setValue("server/host", host);
                settings.setValue("server/port", port);
                settings.setValue("server/tls", tls);
                settings.setValue("settings/notifications", notifications);
                const bool server_changed =
                    host != host_ || port != port_ || tls != use_tls_;
                if (server_changed) {
                    settings.remove("session");
                }
                settings.sync();
                notifications_enabled_ = notifications;
                login_page_->setServerSettings(host, port, tls);
                if (server_changed && logged_in_) {
                    logging_out_ = true;
                    if (network_ != nullptr) {
                        network_->requestStop();
                    }
                    clearSession();
                    login_page_->setServerSettings(host, port, tls);
                    login_page_->setStatus("服务器设置已保存，请重新登录");
                    setStatus("服务器设置已保存，请重新登录", true);
                } else if (server_changed) {
                    setStatus("服务器设置已保存，请重新登录", true);
                } else {
                    setStatus("设置已保存");
                }
                qInfo("settings saved host=%s port=%u tls=%d notifications=%d",
                      qPrintable(host), static_cast<unsigned>(port), tls ? 1 : 0,
                      notifications ? 1 : 0);
            });
    dialog.exec();
}

void MainWindow::sendMarkRead(quint64 peer_id, quint64 message_id) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdPrivateMarkRead;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU64(peer_id);
    writer.putU64(message_id);
    frame.body = writer.data();
    sendFrame(frame);
}

QListWidgetItem* MainWindow::findConversationItem(quint64 peer_id) const {
    for (int i = 0; i < conversation_list_->count(); ++i) {
        QListWidgetItem* item = conversation_list_->item(i);
        if (item->data(Qt::UserRole).toULongLong() == peer_id) {
            return item;
        }
    }
    return nullptr;
}

void MainWindow::updateFriendView(quint64 peer_id, int unread, const QString& nickname,
                                  const QString& last_content, const QString& last_time,
                                  bool online) {
    if (!friend_ids_.contains(peer_id)) {
        return;
    }
    peer_names_.insert(peer_id, nickname);
    chatbox::client::FriendView& view = friend_views_[peer_id];
    view.user_id = peer_id;
    if (!nickname.isEmpty()) {
        view.nickname = nickname;
    }
    view.unread = unread;
    view.last_content = last_content;
    view.last_time = last_time;
    view.online = online;
}

void MainWindow::updateConversationItem(quint64 peer_id, int unread,
                                        const QString& nickname,
                                        const QString& last_content,
                                        const QString& last_time, bool online) {
    updateFriendView(peer_id, unread, nickname, last_content, last_time, online);
    refreshFriendListView();
}

void MainWindow::refreshFriendListView() {
    if (conversation_list_ == nullptr) {
        return;
    }
    const quint64 selected_id =
        context_type_ == ContextType::Private ? current_peer_id_ : 0;
    conversation_list_->clear();

    QVector<chatbox::client::FriendView> online;
    QVector<chatbox::client::FriendView> offline;
    for (const chatbox::client::FriendView& view : friend_views_) {
        if (view.online) {
            online.push_back(view);
        } else {
            offline.push_back(view);
        }
    }
    const auto by_name = [](const chatbox::client::FriendView& a,
                            const chatbox::client::FriendView& b) {
        const QString a_name = a.remark.isEmpty() ? a.nickname : a.remark;
        const QString b_name = b.remark.isEmpty() ? b.nickname : b.remark;
        return a_name.localeAwareCompare(b_name) < 0;
    };
    std::sort(online.begin(), online.end(), by_name);
    std::sort(offline.begin(), offline.end(), by_name);

    const auto add_header = [this](const QString& text) {
        auto* item = new QListWidgetItem(text, conversation_list_);
        item->setFlags(Qt::NoItemFlags);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(0));
        item->setSizeHint(QSize(0, 26));
    };
    const auto add_friend = [this](const chatbox::client::FriendView& view) {
        auto* item = new QListWidgetItem(conversation_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(view.user_id));
        item->setData(Qt::UserRole + 1, view.nickname);
        item->setData(Qt::UserRole + 2, view.unread);
        item->setData(Qt::UserRole + 3, view.online);
        item->setData(Qt::UserRole + 4, view.last_content);
        item->setData(Qt::UserRole + 5, view.last_time);
        item->setData(Qt::UserRole + 6, view.remark);
        QImage image;
        if (avatar_cache_.lookup(view.user_id, image) && !image.isNull()) {
            item->setData(FriendItemDelegate::kAvatarRole,
                          QPixmap::fromImage(
                              chatbox::client::circularAvatar(image, 36)));
        }
        item->setToolTip(QString("ID: %1\n用户名: @%2")
                             .arg(chatbox::client::publicUserId(view.user_id),
                                  view.username));
        item->setSizeHint(QSize(0, 56));
    };

    if (!online.isEmpty()) {
        add_header("在线");
        for (const auto& view : online) {
            add_friend(view);
        }
    }
    if (!offline.isEmpty()) {
        add_header("离线");
        for (const auto& view : offline) {
            add_friend(view);
        }
    }

    if (selected_id != 0) {
        QListWidgetItem* item = findConversationItem(selected_id);
        if (item != nullptr) {
            conversation_list_->setCurrentItem(item);
        }
    }
    refreshUnreadTitle();
}

void MainWindow::updateOwnProfileStrip() {
    if (my_profile_button_ == nullptr || my_profile_avatar_ == nullptr ||
        my_profile_name_ == nullptr || my_profile_uid_ == nullptr) {
        return;
    }
    QString name = nickname_.trimmed();
    if (name.isEmpty()) {
        name = username_.trimmed();
    }
    if (name.isEmpty()) {
        name = "未登录";
    }
    my_profile_name_->setText(name);
    my_profile_uid_->setText(
        user_id_ == 0 ? QString() : chatbox::client::publicUserId(user_id_));

    QPixmap avatar;
    QImage image;
    if (user_id_ != 0 && avatar_cache_.lookup(user_id_, image) && !image.isNull()) {
        avatar = QPixmap::fromImage(chatbox::client::circularAvatar(image, 32));
    } else {
        avatar = avatarIcon(name, user_id_).pixmap(32, 32);
    }
    my_profile_avatar_->setPixmap(avatar);
    my_profile_button_->setToolTip(
        user_id_ == 0
            ? QString("未登录")
            : QString("%1\n%2").arg(name, chatbox::client::publicUserId(user_id_)));
}

bool MainWindow::isCurrentProfileRequest(quint64 user_id, quint32 seq) const {
    return seq != 0 && pending_profile_seqs_.value(user_id) == seq;
}

bool MainWindow::isCurrentAvatarRequest(quint64 user_id, quint32 seq) const {
    return seq != 0 && pending_avatar_seqs_.value(user_id) == seq;
}

void MainWindow::clearPendingProfileRequest(quint64 user_id, quint32 seq) {
    if (isCurrentProfileRequest(user_id, seq)) {
        pending_profile_seqs_.remove(user_id);
        pending_profile_requests_.remove(user_id);
    }
    profile_request_seqs_.remove(seq);
    if (pending_profile_requests_.isEmpty() && profile_timeout_timer_ != nullptr) {
        profile_timeout_timer_->stop();
    }
}

void MainWindow::clearPendingAvatarRequest(quint64 user_id, quint32 seq) {
    if (isCurrentAvatarRequest(user_id, seq)) {
        pending_avatar_seqs_.remove(user_id);
        pending_avatar_versions_.remove(user_id);
        pending_avatar_requests_.remove(user_id);
    }
    avatar_request_seqs_.remove(seq);
    if (pending_avatar_requests_.isEmpty() && avatar_timeout_timer_ != nullptr) {
        avatar_timeout_timer_->stop();
    }
}

void MainWindow::requestProfile(quint64 user_id) {
    if (user_id == 0) {
        return;
    }
    if (!connected_ || !logged_in_) {
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus("网络未连接，无法加载资料", true);
        }
        setStatus("网络未连接，无法加载资料", true);
        return;
    }
    if (pending_profile_requests_.contains(user_id) ||
        pending_profile_seqs_.contains(user_id)) {
        return;
    }

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdProfileGet;
    const quint32 seq = next_seq_++;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU64(user_id);
    frame.body = writer.data();
    if (!sendFrame(frame)) {
        // No request reached the server: keep the dialog usable instead of
        // leaving it in its "loading" busy state.
        clearPendingProfileRequest(user_id, seq);
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus("网络未连接，无法加载资料", true);
        }
        return;
    }
    pending_profile_requests_.insert(user_id);
    pending_profile_seqs_.insert(user_id, seq);
    profile_request_seqs_.insert(seq, user_id);
    qInfo("PROFILE_GET user_id=%llu seq=%u",
          static_cast<unsigned long long>(user_id), seq);
    if (profile_timeout_timer_ != nullptr) {
        profile_timeout_timer_->start();
    }
}

void MainWindow::requestAvatar(quint64 user_id, quint32 version) {
    if (user_id == 0 || !connected_ || !logged_in_) {
        return;
    }
    if (pending_avatar_requests_.contains(user_id) ||
        pending_avatar_seqs_.contains(user_id)) {
        return;
    }

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdAvatarGet;
    const quint32 seq = next_seq_++;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU64(user_id);
    frame.body = writer.data();
    if (!sendFrame(frame)) {
        return;
    }
    pending_avatar_requests_.insert(user_id);
    pending_avatar_seqs_.insert(user_id, seq);
    pending_avatar_versions_.insert(user_id, version);
    avatar_request_seqs_.insert(seq, user_id);
    qInfo("AVATAR_GET user_id=%llu version=%u seq=%u",
          static_cast<unsigned long long>(user_id), version, seq);
    if (avatar_timeout_timer_ != nullptr) {
        avatar_timeout_timer_->start();
    }
}

void MainWindow::requestPeerAvatar(quint64 peer_id) {
    if (peer_id == 0 || peer_id == user_id_) {
        return;
    }
    const auto it = friend_views_.constFind(peer_id);
    if (it != friend_views_.constEnd()) {
        if (it->avatar_version != 0 &&
            avatar_cache_.needsFetch(peer_id, it->avatar_version)) {
            requestAvatar(peer_id, it->avatar_version);
        }
        return;
    }
    // A non-friend has no version in any list yet: ask once with version 0 and
    // cache the version returned by the server, including "no avatar".
    if (!avatar_cache_.contains(peer_id)) {
        requestAvatar(peer_id, 0);
    }
}

void MainWindow::updatePrivateHeaderAvatar(quint64 peer_id, const QString& name) {
    if (context_avatar_ == nullptr || peer_id == 0) {
        return;
    }
    QImage image;
    if (avatar_cache_.lookup(peer_id, image) && !image.isNull()) {
        context_avatar_->setPixmap(
            QPixmap::fromImage(chatbox::client::circularAvatar(image, 32)));
        return;
    }
    QString fallback_name = name;
    if (fallback_name.isEmpty()) {
        fallback_name = peer_names_.value(peer_id);
    }
    if (fallback_name.isEmpty()) {
        fallback_name = chatbox::client::publicUserId(peer_id);
    }
    context_avatar_->setPixmap(avatarIcon(fallback_name, peer_id).pixmap(32, 32));
}

QIcon MainWindow::memberAvatarIcon(quint64 member_id, const QString& nickname,
                                   int size) {
    QImage image;
    if (member_id != 0 && avatar_cache_.lookup(member_id, image) &&
        !image.isNull()) {
        return QIcon(
            QPixmap::fromImage(chatbox::client::circularAvatar(image, size)));
    }
    const QString fallback_name =
        nickname.isEmpty() ? chatbox::client::publicUserId(member_id)
                           : nickname;
    return avatarIcon(fallback_name, member_id);
}

void MainWindow::refreshMemberListAvatar(quint64 user_id) {
    if (member_list_ == nullptr || user_id == 0) {
        return;
    }
    for (int i = 0; i < member_list_->count(); ++i) {
        QListWidgetItem* item = member_list_->item(i);
        if (item != nullptr &&
            item->data(Qt::UserRole).toULongLong() == user_id) {
            item->setIcon(memberAvatarIcon(user_id, item->text(), 28));
            break;
        }
    }
}

void MainWindow::closeProfileDialog() {
    if (profile_dialog_ == nullptr) {
        return;
    }
    ProfileDialog* dialog = profile_dialog_;
    profile_dialog_ = nullptr;
    profile_dialog_user_id_ = 0;
    profile_dialog_fallback_name_.clear();
    profile_dialog_profile_ = chatbox::client::UserProfile();
    dialog->close();
}

void MainWindow::onProfileAvatarReceived(quint64 user_id, quint32 version,
                                         const QImage& image) {
    if (user_id == 0) {
        return;
    }
    quint32 known_version = 0;
    const auto known_view = friend_views_.constFind(user_id);
    if (known_view != friend_views_.constEnd()) {
        known_version = std::max(known_version, known_view->avatar_version);
    }
    const auto known_pending = pending_avatar_versions_.constFind(user_id);
    if (known_pending != pending_avatar_versions_.constEnd()) {
        known_version = std::max(known_version, *known_pending);
    }
    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
        known_version =
            std::max(known_version, profile_dialog_profile_.avatar_version);
    }
    if (user_id == user_id_) {
        // Keep the newest self version even when the self dialog is closed, so
        // an older in-flight avatar/clear response cannot replace it.
        known_version = std::max(known_version, own_avatar_version_);
    }
    if (version < known_version) {
        qInfo("stale avatar discarded user_id=%llu version=%u known=%u",
              static_cast<unsigned long long>(user_id), version, known_version);
        if (avatar_cache_.needsFetch(user_id, known_version)) {
            // The older in-flight request suppressed the request for this
            // version. Its response has now arrived, so fetch the known
            // newer version. requestAvatar() is a no-op if it is pending.
            requestAvatar(user_id, known_version);
        }
        return;
    }

    // AvatarCache::store ignores older versions, so a late stale response
    // cannot replace a newer cached avatar.
    avatar_cache_.store(user_id, version, image);
    qInfo("avatar loaded user_id=%llu version=%u bytes=%d",
          static_cast<unsigned long long>(user_id), version,
          image.isNull() ? 0 : static_cast<int>(image.sizeInBytes()));
    const auto view_it = friend_views_.constFind(user_id);
    const quint32 newer_version =
        view_it != friend_views_.constEnd() && view_it->avatar_version > version
            ? view_it->avatar_version
            : 0;
    if (friend_ids_.contains(user_id)) {
        refreshFriendListView();
    }
    refreshMemberListAvatar(user_id);
    if (user_id == user_id_) {
        own_avatar_version_ = std::max(own_avatar_version_, version);
        updateOwnProfileStrip();
    }
    if (context_type_ == ContextType::Private && current_peer_id_ == user_id &&
        user_id != user_id_) {
        updatePrivateHeaderAvatar(user_id, current_peer_name_);
    }
    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id &&
        version >= profile_dialog_profile_.avatar_version) {
        profile_dialog_profile_.avatar = image;
        profile_dialog_profile_.avatar_version = version;
        profile_dialog_->setAvatar(image);
    }
    if (newer_version != 0 &&
        avatar_cache_.needsFetch(user_id, newer_version)) {
        // A stale response arrived after a push announced a newer version.
        // requestAvatar() is a no-op while that newer request is still pending.
        requestAvatar(user_id, newer_version);
    }
}

void MainWindow::onProfileDataReceived(
    const chatbox::client::UserProfile& profile) {
    if (profile.user_id == 0) {
        return;
    }

    quint32 known_version = 0;
    const auto known_view = friend_views_.constFind(profile.user_id);
    if (known_view != friend_views_.constEnd()) {
        known_version = std::max(known_version, known_view->avatar_version);
    }
    const auto known_pending = pending_avatar_versions_.constFind(profile.user_id);
    if (known_pending != pending_avatar_versions_.constEnd()) {
        known_version = std::max(known_version, *known_pending);
    }
    if (profile_dialog_ != nullptr &&
        profile_dialog_user_id_ == profile.user_id) {
        known_version =
            std::max(known_version, profile_dialog_profile_.avatar_version);
    }
    if (profile.user_id == user_id_) {
        // The self dialog may be closed; own_avatar_version_ still remembers
        // the newest version learned from a self profile/avatar response.
        known_version = std::max(known_version, own_avatar_version_);
    }
    if (profile.avatar_version >= known_version) {
        // AvatarCache::store is also monotonic, which covers self/non-friend
        // responses where no friend view or pending version is available.
        avatar_cache_.store(profile.user_id, profile.avatar_version,
                            profile.avatar);
    } else {
        qInfo("stale profile avatar discarded user_id=%llu version=%u known=%u",
              static_cast<unsigned long long>(profile.user_id),
              profile.avatar_version, known_version);
    }
    qInfo("profile loaded user_id=%llu version=%u relationship=%u",
          static_cast<unsigned long long>(profile.user_id),
          profile.avatar_version, static_cast<unsigned>(profile.relationship));

    const auto friend_it = friend_views_.find(profile.user_id);
    if (friend_it != friend_views_.end()) {
        if (!profile.nickname.isEmpty()) {
            friend_it->nickname = profile.nickname;
        }
        friend_it->remark = profile.remark;
        if (profile.avatar_version >= friend_it->avatar_version) {
            friend_it->avatar_version = profile.avatar_version;
        }
        friend_it->online = profile.online;
        peer_names_.insert(profile.user_id, friend_it->nickname);
        refreshFriendListView();
    }

    if (profile.user_id == user_id_) {
        if (!profile.nickname.isEmpty()) {
            nickname_ = profile.nickname;
        }
        if (!profile.username.isEmpty()) {
            username_ = profile.username;
        }
        own_avatar_version_ =
            std::max(own_avatar_version_, static_cast<quint32>(profile.avatar_version));
        saveSession();
        updateOwnProfileStrip();
    } else if (context_type_ == ContextType::Private &&
               current_peer_id_ == profile.user_id) {
        updatePrivateHeaderAvatar(profile.user_id, current_peer_name_);
    }
    refreshMemberListAvatar(profile.user_id);

    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == profile.user_id) {
        chatbox::client::UserProfile displayed = profile;
        if (displayed.avatar_version < profile_dialog_profile_.avatar_version) {
            displayed.avatar_version = profile_dialog_profile_.avatar_version;
            displayed.avatar = profile_dialog_profile_.avatar;
        }
        profile_dialog_profile_ = displayed;
        profile_dialog_fallback_name_ =
            chatbox::client::profileDisplayName(displayed).trimmed();
        profile_dialog_->setProfile(displayed, displayed.user_id == user_id_);
        profile_dialog_->setBusy(false);
        profile_dialog_->setStatus(QString());
    }
}

void MainWindow::openOwnProfile() {
    if (user_id_ == 0) {
        setStatus("请先登录", true);
        return;
    }
    openProfile(user_id_, nickname_);
}

void MainWindow::openProfile(quint64 user_id, const QString& fallback_name) {
    if (user_id == 0) {
        return;
    }
    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
        profile_dialog_->show();
        profile_dialog_->raise();
        profile_dialog_->activateWindow();
        if (connected_ && logged_in_ &&
            !pending_profile_requests_.contains(user_id) &&
            !pending_profile_seqs_.contains(user_id)) {
            profile_dialog_->setBusy(true);
            profile_dialog_->setStatus("正在加载资料...");
            requestProfile(user_id);
        }
        return;
    }
    closeProfileDialog();

    const bool self = user_id == user_id_;
    chatbox::client::UserProfile fallback;
    fallback.user_id = user_id;
    fallback.relationship = self ? 0 : 2;
    if (self) {
        fallback.username = username_;
        fallback.nickname = nickname_;
        fallback.online = true;
    } else {
        const auto it = friend_views_.constFind(user_id);
        if (it != friend_views_.constEnd()) {
            fallback.username = it->username;
            fallback.nickname = it->nickname;
            fallback.remark = it->remark;
            fallback.avatar_version = it->avatar_version;
            fallback.online = it->online;
            fallback.relationship = 1;
        } else {
            fallback.nickname = fallback_name;
            if (fallback.nickname.isEmpty()) {
                fallback.nickname = peer_names_.value(user_id);
            }
        }
    }
    QImage cached_avatar;
    if (avatar_cache_.lookup(user_id, cached_avatar) && !cached_avatar.isNull()) {
        fallback.avatar = cached_avatar;
    }

    auto* dialog = new ProfileDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    profile_dialog_ = dialog;
    profile_dialog_user_id_ = user_id;
    profile_dialog_fallback_name_ = fallback_name;
    profile_dialog_profile_ = fallback;

    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (profile_dialog_ != dialog) {
            return;
        }
        // Always drop the dangling pointer, including during app close.
        profile_dialog_ = nullptr;
        if (shutting_down_) {
            // Destructor teardown disconnects this handler before deleting
            // dialogs; while members are still alive we only need the pointer
            // cleared and avoid touching the remaining dialog state.
            return;
        }
        profile_dialog_user_id_ = 0;
        profile_dialog_fallback_name_.clear();
        profile_dialog_profile_ = chatbox::client::UserProfile();
    });
    connect(dialog, &ProfileDialog::saveRequested, this,
            &MainWindow::onProfileSaveRequested);
    connect(dialog, &ProfileDialog::messageRequested, this, [this](quint64 id) {
        if (profile_dialog_user_id_ != id) {
            return;
        }
        QString name =
            chatbox::client::profileDisplayName(profile_dialog_profile_).trimmed();
        if (name.isEmpty()) {
            name = profile_dialog_fallback_name_.trimmed();
        }
        if (name.isEmpty()) {
            name = peer_names_.value(id);
        }
        if (name.isEmpty()) {
            name = chatbox::client::publicUserId(id);
        }
        selectPrivate(id, name);
        closeProfileDialog();
    });
    connect(dialog, &ProfileDialog::addFriendRequested, this, [this, dialog](quint64 id) {
        if (!connected_ || !logged_in_) {
            dialog->setStatus("网络未连接，无法发送好友申请", true);
            return;
        }
        dialog->setStatus("正在发送好友申请...");
        sendFriendRequest(id);
    });
    connect(dialog, &ProfileDialog::removeFriendRequested, this, [this, dialog](quint64 id) {
        if (!connected_ || !logged_in_) {
            dialog->setStatus("网络未连接，无法删除好友", true);
            return;
        }
        QString name =
            chatbox::client::profileDisplayName(profile_dialog_profile_).trimmed();
        if (name.isEmpty()) {
            name = profile_dialog_fallback_name_.trimmed();
        }
        if (name.isEmpty()) {
            name = peer_names_.value(id);
        }
        if (name.isEmpty()) {
            name = chatbox::client::publicUserId(id);
        }
        const auto answer = QMessageBox::question(
            dialog, "删除好友", QString("确定要删除好友“%1”吗？").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        sendFriendRemove(id);
        setStatus("正在删除好友...");
        closeProfileDialog();
    });
    connect(dialog, &ProfileDialog::remarkRequested, this, [this](quint64 id) {
        setFriendRemark(id);
    });
    connect(dialog, &ProfileDialog::refreshRequested, this, [this, dialog](quint64 id) {
        if (!connected_ || !logged_in_) {
            dialog->setBusy(false);
            dialog->setStatus("网络未连接，无法刷新", true);
            return;
        }
        dialog->setBusy(true);
        dialog->setStatus("正在加载资料...");
        requestProfile(id);
    });
    connect(dialog, &ProfileDialog::friendRequestsRequested, this,
            &MainWindow::openFriendRequests);

    dialog->setProfile(fallback, self);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();

    if (!connected_ || !logged_in_) {
        dialog->setBusy(false);
        dialog->setStatus("网络未连接，无法加载资料", true);
        setStatus("网络未连接，无法加载资料", true);
        return;
    }
    dialog->setBusy(true);
    dialog->setStatus("正在加载资料...");
    requestProfile(user_id);
}

void MainWindow::onProfileSaveRequested(const QString& nickname,
                                        const QString& signature,
                                        int avatar_action,
                                        const QByteArray& avatar_jpeg) {
    if (user_id_ == 0 || profile_dialog_ == nullptr ||
        profile_dialog_user_id_ != user_id_) {
        // The save button only exists in self mode. Never touch a dialog that
        // is showing another user.
        setStatus("资料窗口已关闭，无法保存", true);
        return;
    }

    const auto fail = [this](const QString& text) {
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus(text, true);
        }
        setStatus(text, true);
    };

    const QString trimmed_nickname = nickname.trimmed();
    const QString trimmed_signature = signature.trimmed();
    const int nickname_count = trimmed_nickname.toUcs4().size();
    const int signature_count = trimmed_signature.toUcs4().size();
    if (nickname_count < 1) {
        fail("昵称不能为空");
        return;
    }
    if (nickname_count > 32) {
        fail(QString("昵称最多 32 个字符（当前 %1 个）").arg(nickname_count));
        return;
    }
    if (signature_count > 128) {
        fail(QString("个性签名最多 128 个字符（当前 %1 个）").arg(signature_count));
        return;
    }
    if (avatar_action < 0 || avatar_action > 2) {
        fail("头像操作无效，请重新选择");
        return;
    }
    const int kAvatarMaxBytes = 48 * 1024;
    if (avatar_action == 1 &&
        (avatar_jpeg.isEmpty() || avatar_jpeg.size() > kAvatarMaxBytes)) {
        fail(avatar_jpeg.isEmpty() ? QString("头像数据无效，请重新选择")
                                   : QString("头像不能超过 48 KiB"));
        return;
    }
    if (!connected_) {
        fail("网络未连接，无法保存资料");
        return;
    }
    if (!logged_in_ || !online_) {
        fail("会话未就绪，无法保存资料");
        return;
    }
    if (pending_profile_update_seq_ != 0) {
        // A second click cannot start another request while the first one is
        // still waiting for its response.
        profile_dialog_->setBusy(true);
        profile_dialog_->setStatus("正在保存资料...");
        return;
    }

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdProfileUpdate;
    const quint32 seq = next_seq_++;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(trimmed_nickname));
    writer.putString(chatbox::toUtf8(trimmed_signature));
    writer.putU8(static_cast<uint8_t>(avatar_action));
    if (avatar_action == 1) {
        writer.putString(std::string(avatar_jpeg.constData(),
                                     static_cast<std::size_t>(avatar_jpeg.size())));
    }
    frame.body = writer.data();

    pending_profile_update_seq_ = seq;
    profile_dialog_->setBusy(true);
    profile_dialog_->setStatus("正在保存资料...");
    if (!sendFrame(frame)) {
        pending_profile_update_seq_ = 0;
        profile_dialog_->setBusy(false);
        profile_dialog_->setStatus("网络未连接，无法保存资料", true);
        setStatus("网络未连接，无法保存资料", true);
        return;
    }

    qInfo("PROFILE_UPDATE nickname=%s signature_bytes=%d avatar_action=%d "
          "avatar_bytes=%d seq=%u",
          qPrintable(trimmed_nickname), trimmed_signature.toUtf8().size(),
          avatar_action, avatar_action == 1 ? avatar_jpeg.size() : 0, seq);
    QTimer::singleShot(8000, this, [this, seq] {
        if (pending_profile_update_seq_ != seq) {
            return;
        }
        pending_profile_update_seq_ = 0;
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
            const bool still_loading =
                pending_profile_requests_.contains(user_id_) ||
                pending_profile_seqs_.contains(user_id_);
            if (!still_loading) {
                profile_dialog_->setBusy(false);
            }
            profile_dialog_->setStatus("保存资料超时，请重试", true);
        }
        setStatus("保存资料超时，请重试", true);
        qWarning("PROFILE_UPDATE timeout seq=%u", seq);
    });
}

void MainWindow::applyOwnProfileUpdate(const QString& nickname,
                                       const QString& signature,
                                       quint32 avatar_version) {
    if (user_id_ == 0) {
        return;
    }
    // Use the version returned by the server instead of inferring a change
    // from the request action: a nickname-only save can reveal a newer version
    // when the self profile request never loaded it.
    quint32 known_version = own_avatar_version_;
    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
        known_version =
            std::max(known_version, profile_dialog_profile_.avatar_version);
    }
    const auto pending_version = pending_avatar_versions_.constFind(user_id_);
    if (pending_version != pending_avatar_versions_.constEnd()) {
        known_version = std::max(known_version, *pending_version);
    }
    const bool version_changed = avatar_version > known_version;

    if (!nickname.isEmpty()) {
        nickname_ = nickname;
        QSettings settings;
        settings.setValue("session/nickname", nickname_);
        settings.sync();
    }
    own_avatar_version_ = std::max(own_avatar_version_, avatar_version);
    if (version_changed) {
        // The response carries no avatar bytes. Drop the old image so a clear
        // is visible immediately and a new upload is filled by AVATAR_GET.
        avatar_cache_.remove(user_id_);
    }
    peer_names_.insert(user_id_, nickname_);
    updateOwnProfileStrip();
    updateRoomMemberNickname(user_id_, nickname_);

    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
        chatbox::client::UserProfile displayed = profile_dialog_profile_;
        displayed.nickname = nickname_;
        displayed.signature = signature;
        displayed.relationship = 0;
        displayed.online = true;
        if (avatar_version >= displayed.avatar_version) {
            if (avatar_version > displayed.avatar_version) {
                displayed.avatar = QImage();
            }
            displayed.avatar_version = avatar_version;
        }
        profile_dialog_profile_ = displayed;
        profile_dialog_fallback_name_ =
            chatbox::client::profileDisplayName(displayed).trimmed();
        const bool still_loading =
            pending_profile_requests_.contains(user_id_) ||
            pending_profile_seqs_.contains(user_id_);
        profile_dialog_->setProfile(displayed, true);
        if (still_loading) {
            profile_dialog_->setBusy(true);
            profile_dialog_->setStatus("正在加载资料...");
        } else {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus("资料已保存");
        }
    }

    if (avatar_version != 0 &&
        avatar_cache_.needsFetch(user_id_, avatar_version)) {
        requestAvatar(user_id_, avatar_version);
    }
    qInfo("profile updated user_id=%llu version=%u avatar_changed=%d",
          static_cast<unsigned long long>(user_id_), avatar_version,
          version_changed ? 1 : 0);
    setStatus("资料已保存");
}

void MainWindow::applyProfileUpdatePush(quint64 user_id, const QString& nickname,
                                        quint32 avatar_version) {
    if (user_id == 0 || user_id == user_id_) {
        return;
    }

    const auto view_it = friend_views_.find(user_id);
    if (view_it != friend_views_.end()) {
        const quint32 previous_version = view_it->avatar_version;
        if (!nickname.isEmpty()) {
            view_it->nickname = nickname;
        }
        if (avatar_version >= view_it->avatar_version) {
            view_it->avatar_version = avatar_version;
        }
        peer_names_.insert(user_id, view_it->nickname);
        if (view_it->avatar_version != previous_version) {
            avatar_cache_.remove(user_id);
            if (view_it->avatar_version != 0) {
                requestAvatar(user_id, view_it->avatar_version);
            }
        }
        refreshFriendListView();
    } else {
        // The friend list may still be loading. The push is still the newest
        // public name/version we know about.
        if (!nickname.isEmpty()) {
            peer_names_.insert(user_id, nickname);
        }
        avatar_cache_.remove(user_id);
        if (avatar_version != 0) {
            requestAvatar(user_id, avatar_version);
        }
    }

    if (!nickname.isEmpty()) {
        updateRoomMemberNickname(user_id, nickname);
    }

    if (context_type_ == ContextType::Private && current_peer_id_ == user_id) {
        QString display_name;
        const auto current_view = friend_views_.constFind(user_id);
        if (current_view != friend_views_.constEnd()) {
            const QString remark = current_view->remark.trimmed();
            display_name = remark.isEmpty() ? current_view->nickname : remark;
        }
        if (display_name.isEmpty()) {
            display_name = nickname;
        }
        if (display_name.isEmpty()) {
            display_name = current_peer_name_;
        }
        if (!display_name.isEmpty() && display_name != current_peer_name_) {
            current_peer_name_ = display_name;
            context_label_->setText("私聊: " + display_name);
        }
        updatePrivateHeaderAvatar(user_id, current_peer_name_);
    }

    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
        chatbox::client::UserProfile displayed = profile_dialog_profile_;
        bool changed = false;
        if (!nickname.isEmpty() && displayed.nickname != nickname) {
            displayed.nickname = nickname;
            changed = true;
        }
        if (avatar_version >= displayed.avatar_version) {
            if (avatar_version > displayed.avatar_version) {
                displayed.avatar = QImage();
                changed = true;
            }
            displayed.avatar_version = avatar_version;
        }
        if (changed) {
            profile_dialog_profile_ = displayed;
            profile_dialog_fallback_name_ =
                chatbox::client::profileDisplayName(displayed).trimmed();
            const bool still_loading =
                pending_profile_requests_.contains(user_id) ||
                pending_profile_seqs_.contains(user_id);
            profile_dialog_->setProfile(displayed, false);
            if (still_loading) {
                profile_dialog_->setBusy(true);
                profile_dialog_->setStatus("正在加载资料...");
            } else {
                profile_dialog_->setBusy(false);
                profile_dialog_->setStatus(QString());
            }
        }
    }
}

void MainWindow::updateRoomMemberNickname(quint64 user_id,
                                          const QString& nickname) {
    if (member_list_ == nullptr || user_id == 0 || nickname.isEmpty()) {
        return;
    }
    for (int i = 0; i < member_list_->count(); ++i) {
        QListWidgetItem* item = member_list_->item(i);
        if (item->data(Qt::UserRole).toULongLong() == user_id) {
            item->setText(nickname);
            item->setIcon(memberAvatarIcon(user_id, nickname, 28));
            break;
        }
    }
}

void MainWindow::setFriendRemark(quint64 peer_id) {
    if (peer_id == 0) {
        return;
    }
    const auto it = friend_views_.constFind(peer_id);
    if (it == friend_views_.constEnd()) {
        return;
    }
    bool ok = false;
    const QString value = QInputDialog::getText(
        this, "好友备注", "备注名（留空恢复昵称）", QLineEdit::Normal, it->remark, &ok);
    if (!ok) {
        return;
    }
    const QString remark = value.trimmed();
    if (remark.toUcs4().size() > 32) {
        setStatus("好友备注不能超过 32 个字符", true);
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendSetRemark;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU64(peer_id);
    writer.putString(chatbox::toUtf8(remark));
    frame.body = writer.data();
    sendFrame(frame);
    setStatus("正在保存好友备注...");
}

void MainWindow::refreshUnreadTitle() {
    int total = 0;
    for (int i = 0; i < conversation_list_->count(); ++i) {
        total += conversation_list_->item(i)->data(Qt::UserRole + 2).toInt();
    }
    setWindowTitle(total > 0 ? QString("ChatBox (%1)").arg(total)
                             : QString("ChatBox"));
}

void MainWindow::showPrivateNotification(const QString& nickname,
                                         const QString& content) {
    if (!notifications_enabled_) {
        return;
    }
    QApplication::alert(this, 3000);
    if (tray_ != nullptr) {
        tray_->showMessage("私聊消息", nickname + ": " + content,
                           QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::onLoginRequested(const QString& host, quint16 port, const QString& username,
                                  const QString& password, bool tls) {
    logging_out_ = false;
    logout_finishing_ = false;
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
        reconnect_scheduled_ = false;
    }
    host_ = host;
    port_ = port;
    use_tls_ = tls;
    username_ = username;
    password_ = password;
    pending_auth_ = PendingAuth::Login;
    login_page_->setBusy(true);
    if (connected_) {
        sendLogin();
    } else {
        startConnection();
    }
}

void MainWindow::onRegisterRequested(const QString& host, quint16 port,
                                     const QString& username, const QString& password,
                                     const QString& nickname, bool tls) {
    logging_out_ = false;
    logout_finishing_ = false;
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
        reconnect_scheduled_ = false;
    }
    host_ = host;
    port_ = port;
    use_tls_ = tls;
    username_ = username;
    password_ = password;
    nickname_ = nickname;
    pending_auth_ = PendingAuth::Register;
    login_page_->setBusy(true);
    if (connected_) {
        sendRegister();
    } else {
        startConnection();
    }
}

void MainWindow::startConnection() {
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
        reconnect_scheduled_ = false;
    }
    if (network_ != nullptr) {
        disconnect(network_, nullptr, this, nullptr);
        network_->requestStop();
        network_->wait();
        delete network_;
        network_ = nullptr;
    }

    const quint64 generation = ++connection_generation_;
    const QString ca_path = QCoreApplication::applicationDirPath() + "/certs/ca.crt";
    network_ = new NetworkThread(host_, port_, use_tls_, ca_path, this);
    NetworkThread* net = network_;
    connect(net, &NetworkThread::connected, this, [this, net, generation] {
        if (network_ == net && generation == connection_generation_) {
            onConnected();
        }
    });
    connect(net, &NetworkThread::disconnected, this,
            [this, net, generation](const QString& reason) {
                if (network_ == net && generation == connection_generation_) {
                    onDisconnected(reason);
                }
            });
    connect(net, &NetworkThread::frameReceived, this,
            [this, net, generation](const QByteArray& data) {
                if (network_ == net && generation == connection_generation_) {
                    onFrameReceived(data);
                }
            });
    network_->start();
    setStatus("正在连接 " + host_ + ":" + QString::number(port_) + "...");
}

void MainWindow::onConnected() {
    connected_ = true;
    qInfo("connected to %s:%u tls=%d", qPrintable(host_),
          static_cast<unsigned>(port_), use_tls_ ? 1 : 0);
    setOnline(false, "已连接，正在恢复会话...");
    if (logged_in_ && !token_.isEmpty()) {
        setStatus("已连接，正在恢复会话...");
        sendResume();
    } else if (pending_auth_ == PendingAuth::Login) {
        sendLogin();
    } else if (pending_auth_ == PendingAuth::Register) {
        sendRegister();
    } else {
        setStatus("已连接");
    }
}

void MainWindow::onDisconnected(const QString& reason) {
    connected_ = false;
    qWarning("disconnected: %s", qPrintable(reason));
    resuming_ = false;
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->stop();
    }
    const bool update_was_pending = pending_profile_update_seq_ != 0;
    pending_profile_update_seq_ = 0;
    pending_profile_requests_.clear();
    profile_request_seqs_.clear();
    pending_profile_seqs_.clear();
    pending_avatar_requests_.clear();
    avatar_request_seqs_.clear();
    pending_avatar_seqs_.clear();
    pending_avatar_versions_.clear();
    friend_request_seqs_.clear();
    if (profile_timeout_timer_ != nullptr) {
        profile_timeout_timer_->stop();
    }
    if (avatar_timeout_timer_ != nullptr) {
        avatar_timeout_timer_->stop();
    }
    if (profile_dialog_ != nullptr) {
        profile_dialog_->setBusy(false);
        profile_dialog_->setStatus(
            update_was_pending && profile_dialog_user_id_ == user_id_
                ? QString("连接已断开，资料保存中断")
                : QString("连接已断开，资料加载中断"),
            true);
    }
    if (shutting_down_) {
        return;
    }
    if (logging_out_) {
        logging_out_ = false;
        return;
    }
    if (logged_in_ && !token_.isEmpty()) {
        outbox_.markAllSendingFailed();
        message_store_.markAllSendingFailed();
        pending_sequences_.clear();
        renderCurrentConversation(ScrollMode::kPreserve);
        setOnline(false, "连接已断开，正在重连...");
        setStatus("连接断开: " + reason + "，正在重连...", true);
        scheduleReconnect();
        return;
    }
    login_page_->setBusy(false);
    login_page_->setStatus("连接断开: " + reason, true);
    stack_->setCurrentWidget(login_page_);
    setStatus("连接断开: " + reason, true);
}

void MainWindow::scheduleReconnect() {
    if (reconnect_scheduled_ || shutting_down_) {
        return;
    }
    reconnect_scheduled_ = true;
    const int exponent = std::min(reconnect_attempt_, 3);
    const int delay = std::min(10000, 1000 * (1 << exponent));
    ++reconnect_attempt_;
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->start(delay);
    }
}

bool MainWindow::sendFrame(const chatbox::Frame& frame) {
    if (network_ == nullptr || !connected_) {
        setStatus("网络未连接", true);
        return false;
    }
    return network_->enqueueFrame(chatbox::encodeToByteArray(frame));
}

void MainWindow::sendLogin() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdLogin;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(username_));
    writer.putString(chatbox::toUtf8(password_));
    frame.body = writer.data();
    sendFrame(frame);
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->start();
    }
    setStatus("正在登录...");
}

void MainWindow::sendRegister() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdRegister;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(username_));
    writer.putString(chatbox::toUtf8(password_));
    writer.putString(chatbox::toUtf8(nickname_));
    frame.body = writer.data();
    sendFrame(frame);
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->start();
    }
    setStatus("正在注册...");
}

void MainWindow::sendResume() {
    resuming_ = true;
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdResume;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(token_));
    frame.body = writer.data();
    sendFrame(frame);
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->start();
    }
}

void MainWindow::requestRoomList() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdRoomList;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
}

void MainWindow::requestConversations() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdPrivateConversations;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
}

void MainWindow::requestFriendList() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendList;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
}

void MainWindow::requestFriendRequests() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendRequests;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
}

void MainWindow::sendFriendRequest(quint64 user_id) {
    if (user_id == 0 || user_id == user_id_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendRequest;
    const quint32 seq = next_seq_++;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU64(user_id);
    frame.body = writer.data();
    if (sendFrame(frame)) {
        friend_request_seqs_.insert(seq, user_id);
        setStatus("好友申请已发送");
    }
}

void MainWindow::sendFriendResponse(quint64 requester_id, bool accept) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendRespond;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU64(requester_id);
    writer.putU8(accept ? 1 : 0);
    frame.body = writer.data();
    sendFrame(frame);
}

void MainWindow::sendFriendRemove(quint64 friend_id) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendRemove;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU64(friend_id);
    frame.body = writer.data();
    sendFrame(frame);
}

void MainWindow::openFriendRequests() {
    if (friend_requests_dialog_ == nullptr) {
        friend_requests_dialog_ = new FriendRequestDialog(this);
        connect(friend_requests_dialog_, &FriendRequestDialog::respondRequested, this,
                [this](quint64 requester_id, bool accept) {
                    sendFriendResponse(requester_id, accept);
                });
        connect(friend_requests_dialog_, &FriendRequestDialog::refreshRequested, this,
                &MainWindow::requestFriendRequests);
        connect(friend_requests_dialog_, &FriendRequestDialog::profileRequested, this,
                [this](quint64 user_id) {
                    const QString fallback =
                        friend_requests_dialog_ != nullptr
                            ? friend_requests_dialog_->currentDisplayName()
                            : QString();
                    openProfile(user_id, fallback);
                });
    }
    requestFriendRequests();
    friend_requests_dialog_->show();
    friend_requests_dialog_->raise();
    friend_requests_dialog_->activateWindow();
}

void MainWindow::updateFriendRequestCount() {
    if (friend_requests_button_ == nullptr) {
        return;
    }
    friend_requests_button_->setText(
        friend_request_count_ > 0
            ? QString("好友申请 (%1)").arg(friend_request_count_)
            : QString("好友申请"));
}

void MainWindow::requestUserSearch(const QString& keyword) {
    if (user_search_dialog_ == nullptr) {
        return;
    }
    if (keyword.toUtf8().size() > 32) {
        user_search_dialog_->setBusy(false);
        user_search_dialog_->setStatus("关键词不能超过 32 字节", true);
        return;
    }
    const quint32 seq = next_seq_++;
    user_search_seq_ = seq;
    user_search_dialog_->setBusy(true);
    user_search_dialog_->setStatus("正在搜索...");

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdUserSearch;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(keyword));
    frame.body = writer.data();
    if (!sendFrame(frame)) {
        user_search_seq_ = 0;
        user_search_dialog_->setBusy(false);
        user_search_dialog_->setStatus("网络未连接，无法搜索", true);
        return;
    }
    if (search_timeout_timer_ != nullptr) {
        search_timeout_timer_->start();
    }
}

void MainWindow::openUserSearch() {
    if (user_search_dialog_ == nullptr) {
        user_search_dialog_ = new UserSearchDialog(this);
        connect(user_search_dialog_, &UserSearchDialog::searchRequested, this,
                &MainWindow::requestUserSearch);
        connect(user_search_dialog_, &UserSearchDialog::userActivated, this,
                [this](quint64 user_id, const QString&, const QString&) {
                    if (user_id == 0 || user_id == user_id_) {
                        return;
                    }
                    if (user_search_dialog_ != nullptr) {
                        user_search_dialog_->close();
                    }
                    sendFriendRequest(user_id);
                });
        connect(user_search_dialog_, &UserSearchDialog::profileRequested, this,
                [this](quint64 user_id) {
                    const QString fallback =
                        user_search_dialog_ != nullptr
                            ? user_search_dialog_->selectedDisplayName()
                            : QString();
                    openProfile(user_id, fallback);
                });
    }
    user_search_dialog_->clearResults();
    user_search_dialog_->show();
    user_search_dialog_->raise();
    user_search_dialog_->activateWindow();
}

void MainWindow::insertMention() {
    if (context_type_ == ContextType::None) {
        setStatus("请先选择房间或私聊", true);
        return;
    }
    QString nickname;
    if (context_type_ == ContextType::Private) {
        nickname = current_peer_name_;
    } else if (member_list_ != nullptr && member_list_->count() > 0) {
        QMenu menu(this);
        for (int i = 0; i < member_list_->count(); ++i) {
            QListWidgetItem* item = member_list_->item(i);
            if (item == nullptr || item->data(Qt::UserRole).toULongLong() == user_id_) {
                continue;
            }
            QAction* action = menu.addAction(item->text());
            action->setData(item->text());
        }
        if (menu.isEmpty()) {
            setStatus("当前没有可提及的成员", true);
            return;
        }
        QAction* selected = menu.exec(QCursor::pos());
        if (selected == nullptr) {
            return;
        }
        nickname = selected->data().toString();
    }
    if (nickname.isEmpty()) {
        setStatus("当前没有可提及的成员", true);
        return;
    }
    input_edit_->insert("@" + nickname + " ");
    input_edit_->setFocus();
}

void MainWindow::setReplyTo(const chatbox::client::ChatMessage& message) {
    reply_to_id_ = message.message_id;
    reply_sender_name_ = message.outgoing ? QString("我") : message.sender_name;
    reply_content_ = message.recalled ? QString("消息已撤回") : message.content;
    if (reply_to_id_ == 0) {
        clearReply();
        return;
    }
    reply_label_->setText("回复 " + reply_sender_name_ + ": " + reply_content_);
    reply_label_->show();
    reply_cancel_button_->show();
    input_edit_->setFocus();
}

void MainWindow::clearReply() {
    reply_to_id_ = 0;
    reply_sender_name_.clear();
    reply_content_.clear();
    if (reply_label_ != nullptr) {
        reply_label_->hide();
    }
    if (reply_cancel_button_ != nullptr) {
        reply_cancel_button_->hide();
    }
}

void MainWindow::sendRecall(quint64 message_id) {
    if (context_type_ == ContextType::None || message_id == 0) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdMessageRecall;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    if (context_type_ == ContextType::Room) {
        writer.putU8(1);
        writer.putU64(static_cast<quint64>(current_room_id_));
    } else {
        writer.putU8(2);
        writer.putU64(current_peer_id_);
    }
    writer.putU64(message_id);
    frame.body = writer.data();
    sendFrame(frame);
}

void MainWindow::showMessageActions(quint64 message_id, const QRect& meta_rect) {
    if (message_action_bar_ == nullptr || chat_view_ == nullptr || message_id == 0) {
        hideMessageActionBar();
        return;
    }
    const chatbox::client::ChatMessage* message =
        message_store_.findMessage(message_id);
    if (message == nullptr) {
        hideMessageActionBar();
        return;
    }
    const chatbox::client::MessageActions actions =
        chatbox::client::messageActions(*message, user_id_);
    if (!actions.can_reply && !actions.can_recall) {
        hideMessageActionBar();
        return;
    }
    message_action_bar_->setActions(actions.can_reply, actions.can_recall);
    const QRect viewport_rect = chat_view_->viewport()->rect();
    const QSize bar_size = message_action_bar_->size();
    const QRect bubble = chat_view_->messageBubbleRect(message_id);
    int x = 0;
    int y = 0;
    bool placed = false;
    if (!bubble.isNull()) {
        // Place the bar in the empty space beside the bubble (Coinbase-style
    // floating action bar),
        // so it neither covers the message nor sits under the pointer.
        const int right_space = viewport_rect.width() - bubble.right() - 1;
        const int left_space = bubble.left();
        if (right_space >= bar_size.width() + 8) {
            x = bubble.right() + 6;
            y = bubble.top() + 2;
            placed = true;
        } else if (left_space >= bar_size.width() + 8) {
            x = bubble.left() - bar_size.width() - 6;
            y = bubble.top() + 2;
            placed = true;
        }
    }
    if (!placed && !bubble.isNull()) {
        // Wide bubble with no room on either side: float the bar in the gap
        // just above the bubble so it never lands under the pointer.
        const int above_y = bubble.top() - bar_size.height() - 2;
        if (above_y >= 4) {
            x = bubble.right() - bar_size.width();
            y = above_y;
            placed = true;
        } else {
            const int below_y = bubble.bottom() + 3;
            if (below_y + bar_size.height() <= viewport_rect.height() - 4) {
                x = bubble.right() - bar_size.width();
                y = below_y;
                placed = true;
            }
        }
    }
    if (!placed) {
        // Fall back to the row above the message time.
        x = meta_rect.isNull() ? viewport_rect.right() - bar_size.width() - 8
                               : meta_rect.right() - bar_size.width();
        y = meta_rect.isNull()
                ? viewport_rect.bottom() - bar_size.height() - 8
                : meta_rect.top() - bar_size.height() - 2;
    }
    x = qBound(4, x, qMax(4, viewport_rect.width() - bar_size.width() - 4));
    y = qBound(4, y, qMax(4, viewport_rect.height() - bar_size.height() - 4));
    message_action_bar_->move(x, y);
    message_action_bar_->raise();
    message_action_bar_->show();
    action_bar_message_id_ = message_id;
}

void MainWindow::hideMessageActionBar() {
    action_bar_message_id_ = 0;
    if (message_action_bar_ != nullptr && message_action_bar_->isVisible()) {
        message_action_bar_->hide();
    }
}

void MainWindow::hideMessageActionsUnlessHovered() {
    if (message_action_bar_ == nullptr || !message_action_bar_->isVisible()) {
        return;
    }
    // Leave events also fire when the pointer moves onto the bar's buttons,
    // and the bar floats a few pixels away from the bubble, so accept a small
    // corridor around it while keeping the bar as long as the pointer is
    // inside.
    const QRect hit_rect = message_action_bar_->rect().adjusted(-10, -10, 10, 10);
    if (hit_rect.contains(message_action_bar_->mapFromGlobal(QCursor::pos()))) {
        return;
    }
    hideMessageActionBar();
}

void MainWindow::showMessageMenu(quint64 message_id, const QString& client_msg_id,
                                 const QPoint& global_pos) {
    const chatbox::client::ChatMessage* message =
        message_id != 0 ? message_store_.findMessage(message_id)
                        : message_store_.findByClientId(client_msg_id);
    if (message == nullptr) {
        return;
    }
    const chatbox::client::MessageActions actions =
        chatbox::client::messageActions(*message, user_id_);
    hideMessageActionBar();

    QMenu menu(this);
    QAction* retry_action = nullptr;
    QAction* reply_action = nullptr;
    QAction* recall_action = nullptr;
    if (actions.can_retry) {
        retry_action = menu.addAction(QString::fromUtf8("重发"));
    }
    if (actions.can_reply) {
        reply_action = menu.addAction(QString::fromUtf8("回复"));
    }
    if (actions.can_recall) {
        recall_action = menu.addAction(QString::fromUtf8("撤回"));
    }
    const bool has_selection = chat_view_->textCursor().hasSelection();
    const bool can_copy = has_selection || !message->content.isEmpty();
    if ((retry_action != nullptr || reply_action != nullptr ||
         recall_action != nullptr) &&
        can_copy) {
        menu.addSeparator();
    }
    QAction* copy_action = nullptr;
    if (can_copy) {
        copy_action = menu.addAction(has_selection ? QString::fromUtf8("复制选中")
                                                   : QString::fromUtf8("复制"));
    }
    if (menu.isEmpty()) {
        return;
    }

    QAction* chosen = menu.exec(global_pos);
    if (chosen == nullptr) {
        return;
    }
    if (chosen == retry_action) {
        retryMessage(message->client_msg_id);
    } else if (chosen == reply_action) {
        setReplyTo(*message);
    } else if (chosen == recall_action) {
        sendRecall(message_id);
    } else if (chosen == copy_action) {
        copyMessageText(*message);
    }
}

void MainWindow::copyMessageText(const chatbox::client::ChatMessage& message) {
    const QTextCursor selection = chat_view_->textCursor();
    if (selection.hasSelection()) {
        QApplication::clipboard()->setText(selection.selectedText());
        return;
    }
    QApplication::clipboard()->setText(message.content);
}

void MainWindow::openMessageSearch() {
    if (context_type_ == ContextType::None) {
        setStatus("请先选择房间或私聊", true);
        return;
    }
    if (message_search_dialog_ == nullptr) {
        message_search_dialog_ = new MessageSearchDialog(this);
        connect(message_search_dialog_, &MessageSearchDialog::searchRequested, this,
                &MainWindow::requestMessageSearch);
    }
    message_search_dialog_->clearResults();
    message_search_dialog_->show();
    message_search_dialog_->raise();
    message_search_dialog_->activateWindow();
}

void MainWindow::requestMessageSearch(const QString& keyword) {
    if (message_search_dialog_ == nullptr || context_type_ == ContextType::None) {
        return;
    }
    if (keyword.toUtf8().size() > 64) {
        message_search_dialog_->setBusy(false);
        message_search_dialog_->setStatus("关键词不能超过 64 字节", true);
        return;
    }
    const quint32 seq = next_seq_++;
    message_search_seq_ = seq;
    message_search_dialog_->setBusy(true);
    message_search_dialog_->setStatus("正在搜索...");

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdMessageSearch;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    if (context_type_ == ContextType::Room) {
        writer.putU8(1);
        writer.putU64(static_cast<quint64>(current_room_id_));
    } else {
        writer.putU8(2);
        writer.putU64(current_peer_id_);
    }
    writer.putString(chatbox::toUtf8(keyword));
    writer.putU16(50);
    frame.body = writer.data();
    if (!sendFrame(frame)) {
        message_search_seq_ = 0;
        message_search_dialog_->setBusy(false);
        message_search_dialog_->setStatus("网络未连接，无法搜索", true);
        return;
    }
    if (message_search_timeout_timer_ != nullptr) {
        message_search_timeout_timer_->start();
    }
}

void MainWindow::showMentionNotification(const QString& nickname,
                                          const QString& content) {
    if (!notifications_enabled_) {
        return;
    }
    QApplication::alert(this, 3000);
    if (tray_ != nullptr) {
        tray_->showMessage("有人@你", nickname + ": " + content,
                           QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::refreshContacts() {
    if (contact_list_ == nullptr) {
        return;
    }
    contact_list_->clear();
    for (const chatbox::client::Contact& contact : contact_store_.contacts()) {
        QString text = contact.nickname;
        if (!contact.username.isEmpty()) {
            text += " (" + contact.username + ")";
        }
        auto* item = new QListWidgetItem(text, contact_list_);
        item->setIcon(avatarIcon(contact.nickname, contact.user_id));
        item->setSizeHint(QSize(0, 48));
        item->setData(Qt::UserRole,
                      QVariant::fromValue<qulonglong>(contact.user_id));
        item->setData(Qt::UserRole + 1, contact.username);
        item->setData(Qt::UserRole + 2, contact.nickname);
    }
}

void MainWindow::onFriendContextMenu(const QPoint& position) {
    QListWidgetItem* item = conversation_list_->itemAt(position);
    if (item == nullptr) {
        return;
    }
    const quint64 friend_id = item->data(Qt::UserRole).toULongLong();
    if (friend_id == 0 || friend_id == user_id_) {
        return;
    }
    const QString nickname = item->data(Qt::UserRole + 1).toString();
    const QString remark = item->data(Qt::UserRole + 6).toString();
    const QString display_name = remark.isEmpty() ? nickname : remark;

    QMenu menu(this);
    QAction* profile_action = menu.addAction("查看资料");
    QAction* remark_action = menu.addAction("设置备注");
    QAction* remove_action = menu.addAction("删除好友");
    QAction* selected = menu.exec(conversation_list_->mapToGlobal(position));
    if (selected == profile_action) {
        openProfile(friend_id, display_name);
    } else if (selected == remark_action) {
        setFriendRemark(friend_id);
    } else if (selected == remove_action) {
        const auto answer = QMessageBox::question(
            this, "删除好友", QString("确定要删除好友“%1”吗？").arg(display_name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            sendFriendRemove(friend_id);
        }
    }
}

void MainWindow::onMemberContextMenu(const QPoint& position) {
    if (member_list_ == nullptr) {
        return;
    }
    QListWidgetItem* item = member_list_->itemAt(position);
    if (item == nullptr) {
        return;
    }
    const quint64 member_id = item->data(Qt::UserRole).toULongLong();
    if (member_id == 0) {
        return;
    }
    QMenu menu(this);
    QAction* profile_action = menu.addAction("查看资料");
    if (menu.exec(member_list_->mapToGlobal(position)) == profile_action) {
        openProfile(member_id, item->text());
    }
}

void MainWindow::onContactActivated(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 user_id = item->data(Qt::UserRole).toULongLong();
    const QString nickname = item->data(Qt::UserRole + 2).toString();
    if (user_id == 0 || user_id == user_id_) {
        return;
    }
    selectPrivate(user_id, nickname);
}

void MainWindow::onContactContextMenu(const QPoint& position) {
    QListWidgetItem* item = contact_list_->itemAt(position);
    if (item == nullptr) {
        return;
    }
    const quint64 user_id = item->data(Qt::UserRole).toULongLong();
    QMenu menu(this);
    QAction* remove_action = menu.addAction("删除联系人");
    if (menu.exec(contact_list_->mapToGlobal(position)) == remove_action) {
        contact_store_.remove(user_id);
        refreshContacts();
    }
}

void MainWindow::requestMembers(int room_id) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdRoomMembers;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    frame.body = writer.data();
    sendFrame(frame);
}

void MainWindow::requestHistory(int kind, quint64 target, quint64 before_id) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdHistory;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU8(static_cast<uint8_t>(kind));
    writer.putU64(target);
    writer.putU64(before_id);
    writer.putU16(50);
    frame.body = writer.data();
    loading_older_history_ = before_id != 0;
    load_more_action_->setEnabled(false);
    sendFrame(frame);
}

void MainWindow::onSendClicked() {
    if (context_type_ == ContextType::None) {
        setStatus("请先选择房间或私聊", true);
        return;
    }
    if (!online_) {
        setStatus("当前离线，无法发送", true);
        return;
    }
    const QString text = input_edit_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (text.toUtf8().size() > 2000) {
        setStatus("消息不能超过 2000 字节", true);
        return;
    }

    const QString client_msg_id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    chatbox::client::ChatMessage message;
    message.client_msg_id = client_msg_id;
    message.context = context_type_ == ContextType::Room
                          ? chatbox::client::ChatContext::kRoom
                          : chatbox::client::ChatContext::kPrivate;
    message.target_id = context_type_ == ContextType::Room
                            ? static_cast<quint64>(current_room_id_)
                            : current_peer_id_;
    message.sender_id = user_id_;
    message.sender_name = nickname_;
    message.content = text;
    message.created_at_utc = QDateTime::currentDateTimeUtc();
    message.reply_to_id = reply_to_id_;
    message.reply_sender_name = reply_sender_name_;
    message.reply_content = reply_content_;
    message.outgoing = true;
    message.status = chatbox::client::MessageStatus::kSending;

    outbox_.add(message, message.created_at_utc);
    message_store_.addPending(message);
    input_edit_->clear();
    renderCurrentConversation(ScrollMode::kBottom);
    sendMessage(message);
    clearReply();
}

void MainWindow::sendMessage(const chatbox::client::ChatMessage& message) {
    chatbox::Frame frame;
    chatbox::ByteWriter writer;
    if (message.context == chatbox::client::ChatContext::kRoom) {
        frame.header.cmd = chatbox::kCmdChatSend;
        writer.putU32(static_cast<uint32_t>(message.target_id));
    } else {
        frame.header.cmd = chatbox::kCmdPrivateSend;
        writer.putU64(message.target_id);
    }
    writer.putString(chatbox::toUtf8(message.client_msg_id));
    writer.putString(chatbox::toUtf8(message.content));
    writer.putU64(message.reply_to_id);
    writer.putString(chatbox::toUtf8(message.reply_sender_name));
    writer.putString(chatbox::toUtf8(message.reply_content));
    frame.header.seq = next_seq_++;
    frame.body = writer.data();

    pending_sequences_.insert(frame.header.seq, message.client_msg_id);
    setStatus("正在发送...");
    if (!sendFrame(frame)) {
        pending_sequences_.remove(frame.header.seq);
        outbox_.markFailed(message.client_msg_id);
        message_store_.markFailed(message.client_msg_id);
        renderCurrentConversation(ScrollMode::kPreserve);
        setStatus("发送失败：网络未连接", true);
        qWarning("send failed: network unavailable client_msg_id=%s",
                 qPrintable(message.client_msg_id));
    }
}

void MainWindow::retryMessage(const QString& client_msg_id) {
    const QVector<chatbox::client::ChatMessage> pending = outbox_.pending();
    for (const chatbox::client::ChatMessage& message : pending) {
        if (message.client_msg_id != client_msg_id) {
            continue;
        }
        if (message.status != chatbox::client::MessageStatus::kFailed) {
            return;
        }
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (!outbox_.markSending(client_msg_id, now)) {
            return;
        }
        message_store_.markSending(client_msg_id);
        renderCurrentConversation(ScrollMode::kPreserve);
        sendMessage(message);
        return;
    }
}

void MainWindow::onSendTimeout() {
    const QVector<QString> expired =
        outbox_.expireSending(QDateTime::currentDateTimeUtc(), 10);
    if (expired.isEmpty()) {
        return;
    }
    bool current_changed = false;
    for (const QString& client_msg_id : expired) {
        if (message_store_.markFailed(client_msg_id)) {
            current_changed = true;
        }
    }
    if (current_changed) {
        renderCurrentConversation(ScrollMode::kPreserve);
    }
    setStatus("消息发送超时，请点击重发", true);
}

void MainWindow::onAuthTimeout() {
    if (resuming_) {
        resuming_ = false;
        setOnline(false, "恢复会话超时，正在重连...");
        setStatus("恢复会话超时，正在重连...", true);
        if (network_ != nullptr) {
            network_->requestStop();
        }
        scheduleReconnect();
        return;
    }
    if (pending_auth_ != PendingAuth::None) {
        pending_auth_ = PendingAuth::None;
        login_page_->setBusy(false);
        login_page_->setStatus("服务器响应超时，请重试", true);
        setStatus("服务器响应超时，请重试", true);
        if (network_ != nullptr) {
            network_->requestStop();
        }
    }
}

void MainWindow::restorePendingForCurrentContext() {
    chatbox::client::ChatContext context;
    quint64 target_id = 0;
    if (!currentChatContext(context, target_id)) {
        return;
    }
    for (const chatbox::client::ChatMessage& message : outbox_.pending()) {
        if (message.context == context && message.target_id == target_id) {
            message_store_.addPending(message);
        }
    }
}

void MainWindow::setOnline(bool online, const QString& text) {
    online_ = online;
    if (connection_banner_ != nullptr) {
        if (online) {
            connection_banner_->hide();
        } else {
            connection_banner_->setText(text.isEmpty() ? QString("连接已断开")
                                                       : text);
            connection_banner_->setStyleSheet(
                "background-color: " + QString::fromLatin1(theme::kBannerErrorBg) +
                "; color: " + QString::fromLatin1(theme::kSemanticDown) +
                "; padding: 6px; border: 1px solid " +
                QString::fromLatin1(theme::kBannerErrorLine) + ";");
            connection_banner_->show();
        }
    }
    const bool can_send = online_ && context_type_ != ContextType::None;
    if (input_edit_ != nullptr) {
        input_edit_->setEnabled(can_send);
    }
    if (send_button_ != nullptr) {
        send_button_->setEnabled(can_send);
    }
}

void MainWindow::onRoomClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const int room_id = item->data(Qt::UserRole).toInt();
    const QString name = item->data(Qt::UserRole + 1).toString();
    selectRoom(room_id, name);
}

void MainWindow::selectRoom(int room_id, const QString& name) {
    context_type_ = ContextType::Room;
    current_room_id_ = room_id;
    current_peer_id_ = 0;
    current_peer_name_ = name;
    context_label_->setText("房间: " + name);
    mention_button_->show();
    if (context_avatar_ != nullptr) {
        context_avatar_->setPixmap(
            QPixmap::fromImage(chatbox::client::roomAvatar(32)));
        context_avatar_->setCursor(Qt::ArrowCursor);
    }
    chat_view_->clear();
    chat_view_->clearHoverState();
    hideMessageActionBar();
    if (input_edit_ != nullptr) {
        input_edit_->clear();
    }
    clearReply();
    message_store_.reset(chatbox::client::ChatContext::kRoom,
                         static_cast<quint64>(room_id));
    restorePendingForCurrentContext();
    renderCurrentConversation(ScrollMode::kBottom);
    member_list_->clear();
    load_more_action_->setEnabled(false);
    has_more_ = false;
    loading_older_history_ = false;
    next_before_id_ = 0;

    chatbox::Frame join;
    join.header.cmd = chatbox::kCmdJoinRoom;
    join.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    join.body = writer.data();
    sendFrame(join);

    requestMembers(room_id);
    requestHistory(1, static_cast<quint64>(room_id), 0);
    input_edit_->setEnabled(online_);
    send_button_->setEnabled(online_);
    setStatus("正在进入房间: " + name);
}

void MainWindow::onConversationClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 peer_id = item->data(Qt::UserRole).toULongLong();
    if (peer_id == 0) {
        return;
    }
    const QString nickname = item->data(Qt::UserRole + 1).toString();
    const QString remark = item->data(Qt::UserRole + 6).toString();
    selectPrivate(peer_id, remark.isEmpty() ? nickname : remark);
}

void MainWindow::onFriendAvatarClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 peer_id = item->data(Qt::UserRole).toULongLong();
    if (peer_id == 0) {
        return;
    }
    const QString nickname = item->data(Qt::UserRole + 1).toString();
    const QString remark = item->data(Qt::UserRole + 6).toString();
    openProfile(peer_id, remark.isEmpty() ? nickname : remark);
}

void MainWindow::selectPrivate(quint64 peer_id, const QString& nickname) {
    context_type_ = ContextType::Private;
    current_room_id_ = 0;
    current_peer_id_ = peer_id;
    current_peer_name_ = nickname;
    peer_names_.insert(peer_id, nickname);
    context_label_->setText("私聊: " + nickname);
    mention_button_->hide();
    updatePrivateHeaderAvatar(peer_id, nickname);
    context_avatar_->setCursor(Qt::PointingHandCursor);
    requestPeerAvatar(peer_id);
    chat_view_->clear();
    chat_view_->clearHoverState();
    hideMessageActionBar();
    if (input_edit_ != nullptr) {
        input_edit_->clear();
    }
    clearReply();
    message_store_.reset(chatbox::client::ChatContext::kPrivate, peer_id);
    restorePendingForCurrentContext();
    renderCurrentConversation(ScrollMode::kBottom);
    member_list_->clear();
    load_more_action_->setEnabled(false);
    has_more_ = false;
    loading_older_history_ = false;
    next_before_id_ = 0;
    if (QListWidgetItem* item = findConversationItem(peer_id)) {
        item->setData(Qt::UserRole + 2, 0);
        QString text = item->text();
        if (text.startsWith('(')) {
            const int end = text.indexOf(") ");
            if (end > 0) {
                text = text.mid(end + 2);
            }
        }
        item->setText(text);
        refreshUnreadTitle();
    }
    requestHistory(2, peer_id, 0);
    input_edit_->setEnabled(online_);
    send_button_->setEnabled(online_);
    setStatus("正在加载与 " + nickname + " 的私聊历史");
}

void MainWindow::onMemberDoubleClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 peer_id = item->data(Qt::UserRole).toULongLong();
    if (peer_id == 0 || peer_id == user_id_) {
        return;
    }
    const QString nickname = item->text();
    selectPrivate(peer_id, nickname);
}

void MainWindow::addConversation(quint64 peer_id, const QString& nickname) {
    peer_names_.insert(peer_id, nickname);
    for (int i = 0; i < conversation_list_->count(); ++i) {
        QListWidgetItem* existing = conversation_list_->item(i);
        if (existing->data(Qt::UserRole).toULongLong() == peer_id) {
            existing->setText(nickname);
            existing->setData(Qt::UserRole + 1, nickname);
            return;
        }
    }
    auto* item = new QListWidgetItem(nickname, conversation_list_);
    item->setIcon(avatarIcon(nickname, peer_id));
    item->setSizeHint(QSize(0, 48));
    item->setSizeHint(QSize(0, 28));
    item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(peer_id));
    item->setData(Qt::UserRole + 1, nickname);
}

void MainWindow::onLoadMoreClicked() {
    if (!has_more_ || context_type_ == ContextType::None) {
        return;
    }
    if (context_type_ == ContextType::Room) {
        requestHistory(1, static_cast<quint64>(current_room_id_), next_before_id_);
    } else {
        requestHistory(2, current_peer_id_, next_before_id_);
    }
}

bool MainWindow::currentContextMatches(int kind, quint64 target) const {
    if (kind == 1) {
        return context_type_ == ContextType::Room &&
               target == static_cast<quint64>(current_room_id_);
    }
    if (kind == 2) {
        return context_type_ == ContextType::Private && target == current_peer_id_;
    }
    return false;
}

void MainWindow::onFrameReceived(const QByteArray& data) {
    chatbox::Frame frame;
    if (!chatbox::decodeFromByteArray(data, frame)) {
        setStatus("收到非法协议帧", true);
        return;
    }
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());

    switch (frame.header.cmd) {
        case chatbox::kCmdPing:
            break;
        case chatbox::kCmdLogin: {
            uint64_t user_id = 0;
            QString nickname;
            QString token;
            if (!reader.getU64(user_id) || !readString(reader, nickname) ||
                !readString(reader, token)) {
                pending_auth_ = PendingAuth::None;
                if (auth_timeout_timer_ != nullptr) {
                    auth_timeout_timer_->stop();
                }
                login_page_->setBusy(false);
                login_page_->setStatus("登录响应格式错误", true);
                setStatus("登录响应格式错误", true);
                return;
            }
            user_id_ = user_id;
            nickname_ = nickname;
            token_ = token;
            password_.clear();
            login_page_->clearPassword();
            logged_in_ = true;
            pending_auth_ = PendingAuth::None;
            resuming_ = false;
            if (auth_timeout_timer_ != nullptr) {
                auth_timeout_timer_->stop();
            }
            reconnect_attempt_ = 0;
            own_avatar_version_ = 0;
            pending_profile_update_seq_ = 0;
            if (reconnect_timer_ != nullptr) {
                reconnect_timer_->stop();
                reconnect_scheduled_ = false;
            }
            qInfo("login ok user_id=%llu",
                  static_cast<unsigned long long>(user_id_));
            saveSession();
            login_page_->setBusy(false);
            stack_->setCurrentWidget(chat_page_);
            updateOwnProfileStrip();
            pending_profile_requests_.clear();
            profile_request_seqs_.clear();
            pending_profile_seqs_.clear();
            pending_avatar_requests_.clear();
            avatar_request_seqs_.clear();
            pending_avatar_seqs_.clear();
            pending_avatar_versions_.clear();
            if (profile_timeout_timer_ != nullptr) {
                profile_timeout_timer_->stop();
            }
            if (avatar_timeout_timer_ != nullptr) {
                avatar_timeout_timer_->stop();
            }
            setOnline(true);
            resetContext();
            requestRoomList();
            requestConversations();
            requestFriendList();
            requestFriendRequests();
            requestProfile(user_id_);
            setStatus("登录成功");
            break;
        }
        case chatbox::kCmdLogout:
            finishLogout();
            break;
        case chatbox::kCmdRegister: {
            uint64_t user_id = 0;
            if (!reader.getU64(user_id)) {
                pending_auth_ = PendingAuth::None;
                if (auth_timeout_timer_ != nullptr) {
                    auth_timeout_timer_->stop();
                }
                login_page_->setBusy(false);
                login_page_->setStatus("注册响应格式错误", true);
                setStatus("注册响应格式错误", true);
                return;
            }
            Q_UNUSED(user_id);
            pending_auth_ = PendingAuth::Login;
            if (auth_timeout_timer_ != nullptr) {
                auth_timeout_timer_->stop();
            }
            setStatus("注册成功，正在登录...");
            sendLogin();
            break;
        }
        case chatbox::kCmdResume: {
            uint64_t user_id = 0;
            QString nickname;
            if (!reader.getU64(user_id) || !readString(reader, nickname)) {
                resuming_ = false;
                if (auth_timeout_timer_ != nullptr) {
                    auth_timeout_timer_->stop();
                }
                login_page_->setBusy(false);
                setOnline(false, "恢复会话响应异常，正在重连...");
                setStatus("恢复会话响应格式错误", true);
                if (network_ != nullptr) {
                    network_->requestStop();
                }
                scheduleReconnect();
                return;
            }
            const ContextType previous_context = context_type_;
            const int previous_room = current_room_id_;
            const quint64 previous_peer = current_peer_id_;
            const QString previous_name = current_peer_name_;
            user_id_ = user_id;
            nickname_ = nickname;
            logged_in_ = true;
            resuming_ = false;
            if (auth_timeout_timer_ != nullptr) {
                auth_timeout_timer_->stop();
            }
            reconnect_attempt_ = 0;
            pending_profile_update_seq_ = 0;
            if (reconnect_timer_ != nullptr) {
                reconnect_timer_->stop();
                reconnect_scheduled_ = false;
            }
            qInfo("resume ok user_id=%llu",
                  static_cast<unsigned long long>(user_id_));
            saveSession();
            stack_->setCurrentWidget(chat_page_);
            updateOwnProfileStrip();
            pending_profile_requests_.clear();
            profile_request_seqs_.clear();
            pending_profile_seqs_.clear();
            pending_avatar_requests_.clear();
            avatar_request_seqs_.clear();
            pending_avatar_seqs_.clear();
            pending_avatar_versions_.clear();
            if (profile_timeout_timer_ != nullptr) {
                profile_timeout_timer_->stop();
            }
            if (avatar_timeout_timer_ != nullptr) {
                avatar_timeout_timer_->stop();
            }
            setOnline(true);
            if (previous_context == ContextType::Room && previous_room > 0) {
                selectRoom(previous_room, previous_name);
            } else if (previous_context == ContextType::Private && previous_peer != 0) {
                selectPrivate(previous_peer, previous_name);
            } else {
                resetContext();
            }
            requestRoomList();
            requestConversations();
            requestFriendList();
            requestFriendRequests();
            requestProfile(user_id_);
            if (profile_dialog_ != nullptr &&
                profile_dialog_user_id_ != user_id_) {
                requestProfile(profile_dialog_user_id_);
            }
            setStatus("会话已恢复");
            break;
        }
        case chatbox::kCmdRoomList: {
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("房间列表格式错误", true);
                return;
            }
            room_list_->clear();
            const int selected_room = current_room_id_;
            bool hall_added = false;
            for (uint16_t i = 0; i < count; ++i) {
                uint32_t id = 0;
                QString name;
                uint32_t max_members = 0;
                uint32_t online = 0;
                if (!reader.getU32(id) || !readString(reader, name) ||
                    !reader.getU32(max_members) || !reader.getU32(online)) {
                    setStatus("房间列表格式错误", true);
                    return;
                }
                if (hall_added) {
                    continue;
                }
                hall_added = true;
                auto* item = new QListWidgetItem(
                    QString("%1  (%2/%3)").arg(name).arg(online).arg(max_members),
                    room_list_);
                item->setIcon(
                    QIcon(QPixmap::fromImage(chatbox::client::roomAvatar(36))));
                item->setSizeHint(QSize(0, 48));
                item->setData(Qt::UserRole, static_cast<uint>(id));
                item->setData(Qt::UserRole + 1, name);
            }
            if (context_type_ == ContextType::None && room_list_->count() > 0) {
                QListWidgetItem* first = room_list_->item(0);
                selectRoom(first->data(Qt::UserRole).toInt(),
                           first->data(Qt::UserRole + 1).toString());
            } else if (context_type_ == ContextType::Room) {
                for (int i = 0; i < room_list_->count(); ++i) {
                    QListWidgetItem* item = room_list_->item(i);
                    if (item->data(Qt::UserRole).toInt() == selected_room) {
                        room_list_->setCurrentItem(item);
                        break;
                    }
                }
            }
            break;
        }
        case chatbox::kCmdJoinRoom:
            setStatus("已加入房间");
            requestMembers(current_room_id_);
            requestRoomList();
            break;
        case chatbox::kCmdRoomMembers: {
            uint32_t room_id = 0;
            uint16_t count = 0;
            if (!reader.getU32(room_id) || !reader.getU16(count)) {
                setStatus("成员列表格式错误", true);
                return;
            }
            if (context_type_ != ContextType::Room ||
                room_id != static_cast<uint32_t>(current_room_id_)) {
                break;
            }
            member_list_->clear();
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t member_id = 0;
                QString nickname;
                if (!reader.getU64(member_id) || !readString(reader, nickname)) {
                    setStatus("成员列表格式错误", true);
                    return;
                }
                peer_names_.insert(member_id, nickname);
                auto* item = new QListWidgetItem(nickname, member_list_);
                item->setIcon(memberAvatarIcon(member_id, nickname, 28));
                item->setSizeHint(QSize(0, 48));
                item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(member_id));
                if (member_id == user_id_) {
                    // requestPeerAvatar() skips self; ask the same way so the
                    // own row fills right after a fresh login too.
                    if (!avatar_cache_.contains(member_id)) {
                        requestAvatar(member_id, 0);
                    }
                } else {
                    // Members are usually non-friends: ask once with version 0
                    // and cache whatever version (including "no avatar") comes
                    // back.
                    requestPeerAvatar(member_id);
                }
            }
            break;
        }
        case chatbox::kCmdChatSendResp: {
            uint64_t message_id = 0;
            QString created_at;
            if (!reader.getU64(message_id) || !readString(reader, created_at)) {
                setStatus("发送响应格式错误", true);
                return;
            }
            const QString client_msg_id = pending_sequences_.take(frame.header.seq);
            if (!client_msg_id.isEmpty()) {
                outbox_.markSent(client_msg_id);
                if (message_store_.markSent(
                        client_msg_id, message_id,
                        chatbox::client::parseServerUtc(created_at))) {
                    renderCurrentConversation(ScrollMode::kPreserve);
                }
            }
            setStatus("消息已发送");
            break;
        }
        case chatbox::kCmdChatPush: {
            uint32_t room_id = 0;
            uint64_t message_id = 0;
            uint64_t sender_id = 0;
            QString nickname;
            QString content;
            QString created_at;
            if (!reader.getU32(room_id) || !reader.getU64(message_id) ||
                !reader.getU64(sender_id) || !readString(reader, nickname) ||
                !readString(reader, content) || !readString(reader, created_at)) {
                return;
            }
            uint64_t reply_to_id = 0;
            QString reply_sender_name;
            QString reply_content;
            if (reader.remaining() >= 8) {
                if (!reader.getU64(reply_to_id) ||
                    !readString(reader, reply_sender_name) ||
                    !readString(reader, reply_content)) {
                    return;
                }
            }
            if (sender_id == user_id_) {
                break;
            }
            if (!nickname_.isEmpty() && content.contains("@" + nickname_)) {
                showMentionNotification(nickname, content);
            }
            peer_names_.insert(sender_id, nickname);
            if (context_type_ == ContextType::Room &&
                room_id == static_cast<uint32_t>(current_room_id_)) {
                chatbox::client::ChatMessage message;
                message.message_id = message_id;
                message.context = chatbox::client::ChatContext::kRoom;
                message.target_id = room_id;
                message.sender_id = sender_id;
                message.sender_name = nickname;
                message.content = content;
                message.created_at_utc = chatbox::client::parseServerUtc(created_at);
                message.reply_to_id = reply_to_id;
                message.reply_sender_name = reply_sender_name;
                message.reply_content = reply_content;
                message.outgoing = false;
                message.status = chatbox::client::MessageStatus::kSent;
                addIncomingMessage(message);
            }
            break;
        }
        case chatbox::kCmdPrivateSendResp: {
            uint64_t message_id = 0;
            QString created_at;
            if (!reader.getU64(message_id) || !readString(reader, created_at)) {
                setStatus("私聊响应格式错误", true);
                return;
            }
            const QString client_msg_id = pending_sequences_.take(frame.header.seq);
            if (!client_msg_id.isEmpty()) {
                outbox_.markSent(client_msg_id);
                if (message_store_.markSent(
                        client_msg_id, message_id,
                        chatbox::client::parseServerUtc(created_at))) {
                    renderCurrentConversation(ScrollMode::kPreserve);
                }
            }
            requestConversations();
            setStatus("私聊已发送");
            break;
        }
        case chatbox::kCmdPrivatePush: {
            uint64_t from_id = 0;
            uint64_t message_id = 0;
            QString nickname;
            QString content;
            QString created_at;
            if (!reader.getU64(from_id) || !reader.getU64(message_id) ||
                !readString(reader, nickname) || !readString(reader, content) ||
                !readString(reader, created_at)) {
                return;
            }
            uint64_t reply_to_id = 0;
            QString reply_sender_name;
            QString reply_content;
            if (reader.remaining() >= 8) {
                if (!reader.getU64(reply_to_id) ||
                    !readString(reader, reply_sender_name) ||
                    !readString(reader, reply_content)) {
                    return;
                }
            }
            if (from_id == user_id_) {
                break;
            }
            peer_names_.insert(from_id, nickname);
            QListWidgetItem* item = findConversationItem(from_id);
            const bool online = item == nullptr
                                    ? true
                                    : item->data(Qt::UserRole + 3).toBool();
            if (context_type_ == ContextType::Private && from_id == current_peer_id_) {
                chatbox::client::ChatMessage message;
                message.message_id = message_id;
                message.context = chatbox::client::ChatContext::kPrivate;
                message.target_id = from_id;
                message.sender_id = from_id;
                message.sender_name = nickname;
                message.content = content;
                message.created_at_utc = chatbox::client::parseServerUtc(created_at);
                message.reply_to_id = reply_to_id;
                message.reply_sender_name = reply_sender_name;
                message.reply_content = reply_content;
                message.outgoing = false;
                message.status = chatbox::client::MessageStatus::kSent;
                addIncomingMessage(message);
                sendMarkRead(from_id, message_id);
                updateConversationItem(from_id, 0, nickname, content, created_at, online);
                refreshUnreadTitle();
            } else {
                if (!friend_ids_.contains(from_id)) {
                    break;
                }
                int unread = item == nullptr
                                 ? 1
                                 : item->data(Qt::UserRole + 2).toInt() + 1;
                updateConversationItem(from_id, unread, nickname, content, created_at, online);
                refreshUnreadTitle();
                showPrivateNotification(nickname, content);
            }
            break;
        }
        case chatbox::kCmdPrivateConversations: {
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("会话列表格式错误", true);
                return;
            }
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t peer_id = 0;
                QString nickname;
                uint64_t last_message_id = 0;
                QString last_content;
                QString last_time;
                uint8_t online = 0;
                uint32_t unread = 0;
                if (!reader.getU64(peer_id) || !readString(reader, nickname) ||
                    !reader.getU64(last_message_id) || !readString(reader, last_content) ||
                    !readString(reader, last_time) || !reader.getU8(online) ||
                    !reader.getU32(unread)) {
                    setStatus("会话列表格式错误", true);
                    return;
                }
                if (!friend_ids_.contains(peer_id)) {
                    continue;
                }
                Q_UNUSED(last_message_id);
                updateFriendView(peer_id, static_cast<int>(unread), nickname,
                                 last_content, last_time, online != 0);
            }
            refreshFriendListView();
            if (context_type_ == ContextType::Private) {
                QListWidgetItem* current = findConversationItem(current_peer_id_);
                if (current != nullptr) {
                    conversation_list_->setCurrentItem(current);
                }
            }
            break;
        }
        case chatbox::kCmdHistoryResp: {
            uint8_t kind = 0;
            uint64_t target = 0;
            uint16_t count = 0;
            uint8_t has_more = 0;
            uint64_t next_before = 0;
            if (!reader.getU8(kind) || !reader.getU64(target) || !reader.getU16(count) ||
                !reader.getU8(has_more) || !reader.getU64(next_before)) {
                setStatus("历史消息格式错误", true);
                return;
            }
            if (!currentContextMatches(kind, target)) {
                break;
            }
            QVector<chatbox::client::ChatMessage> page;
            const chatbox::client::ChatContext chat_context =
                kind == 1 ? chatbox::client::ChatContext::kRoom
                          : chatbox::client::ChatContext::kPrivate;
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t message_id = 0;
                uint64_t sender_id = 0;
                QString nickname;
                QString content;
                QString created_at;
                uint64_t reply_to_id = 0;
                QString reply_sender_name;
                QString reply_content;
                uint8_t recalled = 0;
                if (!reader.getU64(message_id) || !reader.getU64(sender_id) ||
                    !readString(reader, nickname) || !readString(reader, content) ||
                    !readString(reader, created_at) || !reader.getU64(reply_to_id) ||
                    !readString(reader, reply_sender_name) ||
                    !readString(reader, reply_content) || !reader.getU8(recalled)) {
                    setStatus("历史消息格式错误", true);
                    return;
                }
                chatbox::client::ChatMessage message;
                message.message_id = message_id;
                message.context = chat_context;
                message.target_id = target;
                message.sender_id = sender_id;
                message.sender_name = nickname;
                message.content = content;
                message.created_at_utc = chatbox::client::parseServerUtc(created_at);
                message.reply_to_id = reply_to_id;
                message.reply_sender_name = reply_sender_name;
                message.reply_content = reply_content;
                message.recalled = recalled != 0;
                message.outgoing = sender_id == user_id_;
                message.status = chatbox::client::MessageStatus::kSent;
                page.push_back(message);
                peer_names_.insert(sender_id, nickname);
            }

            const bool was_older_page = loading_older_history_;
            if (was_older_page) {
                message_store_.prependHistory(page);
                renderCurrentConversation(ScrollMode::kTop);
            } else {
                message_store_.replaceHistory(page);
                renderCurrentConversation(ScrollMode::kBottom);
            }
            loading_older_history_ = false;
            has_more_ = has_more != 0;
            next_before_id_ = next_before;
            load_more_action_->setEnabled(has_more_);
            if (kind == 2 && !was_older_page) {
                requestConversations();
            }
            if (count == 0) {
                setStatus("暂无历史消息");
            }
            break;
        }
        case chatbox::kCmdUserSearch: {
            if (frame.header.seq != user_search_seq_) {
                break;
            }
            if (search_timeout_timer_ != nullptr) {
                search_timeout_timer_->stop();
            }
            user_search_seq_ = 0;
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("用户搜索响应格式错误", true);
                return;
            }
            QVector<UserSearchResult> results;
            results.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t user_id = 0;
                QString username;
                QString nickname;
                uint8_t online = 0;
                if (!reader.getU64(user_id) || !readString(reader, username) ||
                    !readString(reader, nickname) || !reader.getU8(online)) {
                    setStatus("用户搜索响应格式错误", true);
                    return;
                }
                UserSearchResult result;
                result.user_id = user_id;
                result.username = username;
                result.nickname = nickname;
                result.online = online != 0;
                results.push_back(result);
            }
            if (user_search_dialog_ != nullptr) {
                user_search_dialog_->setResults(results);
                user_search_dialog_->setBusy(false);
            }
            break;
        }
        case chatbox::kCmdMessageRecall: {
            uint8_t kind = 0;
            uint64_t target_id = 0;
            uint64_t message_id = 0;
            if (!reader.getU8(kind) || !reader.getU64(target_id) ||
                !reader.getU64(message_id)) {
                setStatus("撤回响应格式错误", true);
                return;
            }
            const bool matches =
                (kind == 1 && context_type_ == ContextType::Room &&
                 target_id == static_cast<uint64_t>(current_room_id_)) ||
                (kind == 2 && context_type_ == ContextType::Private &&
                 target_id == current_peer_id_);
            if (matches && message_store_.markRecalled(message_id)) {
                renderCurrentConversation(ScrollMode::kPreserve);
            }
            break;
        }
        case chatbox::kCmdMessageSearch: {
            if (frame.header.seq != message_search_seq_) {
                break;
            }
            if (message_search_timeout_timer_ != nullptr) {
                message_search_timeout_timer_->stop();
            }
            message_search_seq_ = 0;
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("消息搜索响应格式错误", true);
                return;
            }
            QVector<MessageSearchResult> results;
            results.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t message_id = 0;
                uint64_t sender_id = 0;
                QString nickname;
                QString content;
                QString created_at;
                uint8_t recalled = 0;
                if (!reader.getU64(message_id) || !reader.getU64(sender_id) ||
                    !readString(reader, nickname) || !readString(reader, content) ||
                    !readString(reader, created_at) || !reader.getU8(recalled)) {
                    setStatus("消息搜索响应格式错误", true);
                    return;
                }
                MessageSearchResult result;
                result.message_id = message_id;
                result.sender_id = sender_id;
                result.nickname = nickname;
                result.content = content;
                result.created_at = created_at;
                result.recalled = recalled != 0;
                results.push_back(result);
            }
            if (message_search_dialog_ != nullptr) {
                message_search_dialog_->setResults(results);
                message_search_dialog_->setBusy(false);
            }
            break;
        }
        case chatbox::kCmdFriendList: {
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("好友列表格式错误", true);
                return;
            }
            conversation_list_->clear();
            friend_ids_.clear();
            friend_views_.clear();
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t friend_id = 0;
                QString username;
                QString nickname;
                uint8_t online = 0;
                QString remark;
                uint32_t avatar_version = 0;
                if (!reader.getU64(friend_id) || !readString(reader, username) ||
                    !readString(reader, nickname) || !reader.getU8(online) ||
                    !readString(reader, remark) || !reader.getU32(avatar_version)) {
                    setStatus("好友列表格式错误", true);
                    return;
                }
                friend_ids_.insert(friend_id);
                chatbox::client::FriendView view;
                view.user_id = friend_id;
                view.username = username;
                view.nickname = nickname;
                view.remark = remark;
                view.avatar_version = avatar_version;
                view.online = online != 0;
                friend_views_.insert(friend_id, view);
            }
            refreshFriendListView();
            for (const chatbox::client::FriendView& view : friend_views_) {
                if (view.avatar_version != 0 &&
                    avatar_cache_.needsFetch(view.user_id, view.avatar_version)) {
                    requestAvatar(view.user_id, view.avatar_version);
                }
            }
            requestConversations();
            break;
        }
        case chatbox::kCmdFriendRequests: {
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("好友申请格式错误", true);
                return;
            }
            QVector<FriendRequestInfo> requests;
            requests.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t requester_id = 0;
                QString username;
                QString nickname;
                QString created_at;
                if (!reader.getU64(requester_id) || !readString(reader, username) ||
                    !readString(reader, nickname) || !readString(reader, created_at)) {
                    setStatus("好友申请格式错误", true);
                    return;
                }
                FriendRequestInfo request;
                request.user_id = requester_id;
                request.username = username;
                request.nickname = nickname;
                request.created_at = created_at;
                requests.push_back(request);
            }
            friend_request_count_ = requests.size();
            updateFriendRequestCount();
            if (friend_requests_dialog_ != nullptr) {
                friend_requests_dialog_->setRequests(requests);
            }
            break;
        }
        case chatbox::kCmdFriendRequest: {
            const quint64 requested_user = friend_request_seqs_.take(frame.header.seq);
            if (profile_dialog_ != nullptr && requested_user != 0 &&
                profile_dialog_user_id_ == requested_user &&
                profile_dialog_profile_.relationship == 2) {
                profile_dialog_profile_.relationship = 3;
                profile_dialog_->setProfile(profile_dialog_profile_, false);
                profile_dialog_->setStatus("好友申请已发送");
            }
            setStatus("好友申请已发送");
            break;
        }
        case chatbox::kCmdFriendRespond:
            setStatus("好友申请已处理");
            requestFriendList();
            requestFriendRequests();
            break;
        case chatbox::kCmdFriendRemove:
            setStatus("已删除好友");
            requestFriendList();
            break;
        case chatbox::kCmdFriendSetRemark:
            setStatus("好友备注已保存");
            requestFriendList();
            if (profile_dialog_ != nullptr &&
                friend_ids_.contains(profile_dialog_user_id_)) {
                requestProfile(profile_dialog_user_id_);
            }
            break;
        case chatbox::kCmdProfileUpdate: {
            const quint32 response_seq = frame.header.seq;
            if (pending_profile_update_seq_ == 0 ||
                response_seq != pending_profile_update_seq_) {
                qWarning("stale profile update response ignored seq=%u pending=%u",
                         response_seq, pending_profile_update_seq_);
                break;
            }
            pending_profile_update_seq_ = 0;

            QString updated_nickname;
            QString updated_signature;
            uint32_t avatar_version = 0;
            if (!readString(reader, updated_nickname) ||
                !readString(reader, updated_signature) ||
                !reader.getU32(avatar_version) ||
                updated_nickname.trimmed().isEmpty()) {
                if (profile_dialog_ != nullptr &&
                    profile_dialog_user_id_ == user_id_) {
                    const bool still_loading =
                        pending_profile_requests_.contains(user_id_) ||
                        pending_profile_seqs_.contains(user_id_);
                    if (!still_loading) {
                        profile_dialog_->setBusy(false);
                    }
                    profile_dialog_->setStatus("资料响应格式错误", true);
                }
                setStatus("资料响应格式错误", true);
                return;
            }
            applyOwnProfileUpdate(updated_nickname, updated_signature,
                                  static_cast<quint32>(avatar_version));
            break;
        }
        case chatbox::kCmdProfileGet: {
            const quint32 response_seq = frame.header.seq;
            const quint64 requested_user = profile_request_seqs_.value(response_seq);
            uint64_t target_id = 0;
            QString username;
            QString nickname;
            QString signature;
            uint32_t avatar_version = 0;
            std::string avatar_bytes;
            uint8_t online = 0;
            uint8_t relationship = 0;
            QString remark;
            if (!reader.getU64(target_id) || !readString(reader, username) ||
                !readString(reader, nickname) || !readString(reader, signature) ||
                !reader.getU32(avatar_version) || !reader.getString(avatar_bytes) ||
                !reader.getU8(online) || !reader.getU8(relationship) ||
                !readString(reader, remark)) {
                if (requested_user != 0) {
                    const bool current_request =
                        isCurrentProfileRequest(requested_user, response_seq);
                    clearPendingProfileRequest(requested_user, response_seq);
                    if (current_request) {
                        if (profile_dialog_ != nullptr &&
                            profile_dialog_user_id_ == requested_user) {
                            profile_dialog_->setBusy(false);
                            profile_dialog_->setStatus("资料响应格式错误", true);
                        }
                        setStatus("资料响应格式错误", true);
                    } else {
                        qWarning("stale malformed profile response ignored seq=%u "
                                 "user_id=%llu",
                                 response_seq,
                                 static_cast<unsigned long long>(requested_user));
                    }
                }
                return;
            }

            chatbox::client::UserProfile profile;
            profile.user_id = target_id;
            profile.username = username;
            profile.nickname = nickname;
            profile.signature = signature;
            profile.avatar_version = avatar_version;
            if (!avatar_bytes.empty()) {
                profile.avatar = QImage::fromData(
                    QByteArray(avatar_bytes.data(),
                               static_cast<int>(avatar_bytes.size())));
            }
            profile.online = online != 0;
            profile.relationship = relationship;
            profile.remark = remark;

            clearPendingProfileRequest(target_id, response_seq);
            if (requested_user != 0 && requested_user != target_id) {
                clearPendingProfileRequest(requested_user, response_seq);
            }
            if (pending_profile_seqs_.contains(target_id)) {
                // A newer request for this user is still in flight. Applying
                // this older response could regress the dialog/friend view.
                qInfo("stale profile response ignored user_id=%llu seq=%u "
                      "pending_seq=%u",
                      static_cast<unsigned long long>(target_id), response_seq,
                      pending_profile_seqs_.value(target_id));
                break;
            }
            onProfileDataReceived(profile);
            break;
        }
        case chatbox::kCmdAvatarGet: {
            const quint32 response_seq = frame.header.seq;
            const quint64 requested_user = avatar_request_seqs_.value(response_seq);
            uint64_t target_id = 0;
            uint32_t avatar_version = 0;
            std::string avatar_bytes;
            if (!reader.getU64(target_id) || !reader.getU32(avatar_version) ||
                !reader.getString(avatar_bytes)) {
                if (requested_user != 0) {
                    const bool current_request =
                        isCurrentAvatarRequest(requested_user, response_seq);
                    clearPendingAvatarRequest(requested_user, response_seq);
                    if (current_request) {
                        setStatus("头像响应格式错误", true);
                    } else {
                        qWarning("stale malformed avatar response ignored seq=%u "
                                 "user_id=%llu",
                                 response_seq,
                                 static_cast<unsigned long long>(requested_user));
                    }
                }
                return;
            }
            QImage image;
            if (!avatar_bytes.empty()) {
                image = QImage::fromData(
                    QByteArray(avatar_bytes.data(),
                               static_cast<int>(avatar_bytes.size())));
            }

            clearPendingAvatarRequest(target_id, response_seq);
            if (requested_user != 0 && requested_user != target_id) {
                clearPendingAvatarRequest(requested_user, response_seq);
            }
            if (pending_avatar_seqs_.contains(target_id)) {
                const quint32 pending_version =
                    pending_avatar_versions_.value(target_id);
                if (pending_version != 0 && avatar_version < pending_version) {
                    qInfo("stale avatar response ignored user_id=%llu version=%u "
                          "pending_version=%u seq=%u",
                          static_cast<unsigned long long>(target_id),
                          avatar_version, pending_version, response_seq);
                    break;
                }
            }
            onProfileAvatarReceived(target_id, avatar_version, image);
            break;
        }
        case chatbox::kCmdProfileUpdatePush: {
            uint64_t pushed_user_id = 0;
            QString pushed_nickname;
            uint32_t pushed_avatar_version = 0;
            if (!reader.getU64(pushed_user_id) ||
                !readString(reader, pushed_nickname) ||
                !reader.getU32(pushed_avatar_version)) {
                return;
            }
            if (pushed_user_id == 0) {
                break;
            }
            if (pushed_user_id == user_id_) {
                // Self updates are applied from the PROFILE_UPDATE response;
                // a self push must never rewrite the private peer header.
                qWarning("self profile update push ignored");
                break;
            }
            applyProfileUpdatePush(
                pushed_user_id, pushed_nickname,
                static_cast<quint32>(pushed_avatar_version));
            break;
        }
        case chatbox::kCmdFriendRequestPush: {
            uint64_t from_id = 0;
            QString username;
            QString nickname;
            if (!reader.getU64(from_id) || !readString(reader, username) ||
                !readString(reader, nickname)) {
                return;
            }
            Q_UNUSED(username);
            requestFriendRequests();
            if (notifications_enabled_ && tray_ != nullptr) {
                tray_->showMessage("好友申请", nickname + " 请求添加你为好友",
                                   QSystemTrayIcon::Information, 3000);
            }
            break;
        }
        case chatbox::kCmdFriendAcceptPush: {
            uint64_t friend_id = 0;
            QString username;
            QString nickname;
            uint8_t online = 0;
            if (!reader.getU64(friend_id) || !readString(reader, username) ||
                !readString(reader, nickname) || !reader.getU8(online)) {
                return;
            }
            Q_UNUSED(username);
            friend_ids_.insert(friend_id);
            updateConversationItem(friend_id, 0, nickname, "", "", online != 0);
            if (notifications_enabled_ && tray_ != nullptr) {
                tray_->showMessage("好友申请", nickname + " 已添加你为好友",
                                   QSystemTrayIcon::Information, 3000);
            }
            requestFriendList();
            break;
        }
        case chatbox::kCmdError: {
            uint16_t code = 0;
            QString message;
            if (!reader.getU16(code) || !readString(reader, message)) {
                setStatus("错误响应格式错误", true);
                return;
            }
            handleError(code, message, frame.header.seq);
            break;
        }
        default:
            break;
    }
}

void MainWindow::handleError(quint16 code, const QString& message, quint32 seq) {
    const QString text = errorText(code, message);
    qWarning("server error code=%u message=%s", static_cast<unsigned>(code),
             qPrintable(text));
    const bool was_resuming = resuming_;
    resuming_ = false;
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->stop();
    }
    login_page_->setBusy(false);
    if (!logged_in_) {
        login_page_->setStatus(text, true);
    }

    const QString pending_id = pending_sequences_.take(seq);
    bool pending_failed = false;
    if (!pending_id.isEmpty()) {
        const bool was_pending = outbox_.markFailed(pending_id);
        if (was_pending) {
            pending_failed = true;
            const bool current_changed = message_store_.markFailed(pending_id);
            if (current_changed) {
                renderCurrentConversation(ScrollMode::kPreserve);
            }
        }
    }

    if (code == 1002 || code == 1009 || code == 1010) {
        clearSession();
        setStatus(QString("会话失效 (%1): %2").arg(code).arg(text), true);
        login_page_->setStatus(QString("会话失效: %1").arg(text), true);
        return;
    }

    if (pending_profile_update_seq_ != 0 && seq == pending_profile_update_seq_) {
        pending_profile_update_seq_ = 0;
        const QString update_error = QString("保存资料失败: %1").arg(text);
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
            const bool still_loading =
                pending_profile_requests_.contains(user_id_) ||
                pending_profile_seqs_.contains(user_id_);
            if (!still_loading) {
                profile_dialog_->setBusy(false);
            }
            profile_dialog_->setStatus(update_error, true);
        }
        setStatus(update_error, true);
        return;
    }

    if (profile_request_seqs_.contains(seq)) {
        const quint64 profile_user = profile_request_seqs_.value(seq);
        const bool current_request =
            isCurrentProfileRequest(profile_user, seq);
        clearPendingProfileRequest(profile_user, seq);
        if (current_request) {
            if (profile_dialog_ != nullptr &&
                profile_dialog_user_id_ == profile_user) {
                profile_dialog_->setBusy(false);
                profile_dialog_->setStatus(
                    QString("加载资料失败: %1").arg(text), true);
            }
            setStatus(QString("加载资料失败: %1").arg(text), true);
        } else {
            qWarning("stale profile error ignored seq=%u user_id=%llu: %s",
                     seq, static_cast<unsigned long long>(profile_user),
                     qPrintable(text));
        }
        return;
    }

    if (avatar_request_seqs_.contains(seq)) {
        const quint64 avatar_user = avatar_request_seqs_.value(seq);
        const bool current_request =
            isCurrentAvatarRequest(avatar_user, seq);
        clearPendingAvatarRequest(avatar_user, seq);
        if (current_request) {
            qWarning("avatar request failed user_id=%llu: %s",
                     static_cast<unsigned long long>(avatar_user),
                     qPrintable(text));
        } else {
            qWarning("stale avatar error ignored seq=%u user_id=%llu: %s",
                     seq, static_cast<unsigned long long>(avatar_user),
                     qPrintable(text));
        }
        return;
    }

    if (friend_request_seqs_.contains(seq)) {
        const quint64 friend_user = friend_request_seqs_.take(seq);
        if (profile_dialog_ != nullptr &&
            profile_dialog_user_id_ == friend_user) {
            profile_dialog_->setStatus(QString("好友申请失败: %1").arg(text), true);
        }
        setStatus(QString("好友申请失败: %1").arg(text), true);
        return;
    }

    if (was_resuming) {
        setOnline(false, "恢复会话失败，正在重连...");
        setStatus(QString("恢复会话失败: %1").arg(text), true);
        if (network_ != nullptr) {
            network_->requestStop();
        }
        scheduleReconnect();
        return;
    }

    if (user_search_seq_ != 0 && seq == user_search_seq_) {
        user_search_seq_ = 0;
        if (search_timeout_timer_ != nullptr) {
            search_timeout_timer_->stop();
        }
        if (user_search_dialog_ != nullptr) {
            user_search_dialog_->setBusy(false);
            user_search_dialog_->setStatus(QString("搜索失败: %1").arg(text), true);
        }
        setStatus(QString("搜索失败: %1").arg(text), true);
        return;
    }

    if (message_search_seq_ != 0 && seq == message_search_seq_) {
        message_search_seq_ = 0;
        if (message_search_timeout_timer_ != nullptr) {
            message_search_timeout_timer_->stop();
        }
        if (message_search_dialog_ != nullptr) {
            message_search_dialog_->setBusy(false);
            message_search_dialog_->setStatus(QString("搜索失败: %1").arg(text), true);
        }
        setStatus(QString("搜索失败: %1").arg(text), true);
        return;
    }

    if (pending_failed) {
        setStatus(QString("发送失败: %1").arg(text), true);
        return;
    }
    setStatus(QString("错误 %1: %2").arg(code).arg(text), true);
}

void MainWindow::clearSession() {
    logged_in_ = false;
    connected_ = false;
    ++connection_generation_;
    if (network_ != nullptr) {
        network_->requestStop();
    }
    resuming_ = false;
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->stop();
    }
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
        reconnect_scheduled_ = false;
    }
    token_.clear();
    password_.clear();
    user_id_ = 0;
    nickname_.clear();
    pending_auth_ = PendingAuth::None;
    outbox_.clear();
    pending_sequences_.clear();
    friend_ids_.clear();
    friend_views_.clear();
    pending_profile_requests_.clear();
    profile_request_seqs_.clear();
    pending_profile_seqs_.clear();
    pending_avatar_requests_.clear();
    avatar_request_seqs_.clear();
    pending_avatar_seqs_.clear();
    pending_avatar_versions_.clear();
    friend_request_seqs_.clear();
    pending_profile_update_seq_ = 0;
    own_avatar_version_ = 0;
    if (profile_timeout_timer_ != nullptr) {
        profile_timeout_timer_->stop();
    }
    if (avatar_timeout_timer_ != nullptr) {
        avatar_timeout_timer_->stop();
    }
    closeProfileDialog();
    avatar_cache_.clear();
    friend_request_count_ = 0;
    updateFriendRequestCount();
    user_search_seq_ = 0;
    if (search_timeout_timer_ != nullptr) {
        search_timeout_timer_->stop();
    }
    if (user_search_dialog_ != nullptr) {
        user_search_dialog_->close();
        user_search_dialog_->clearResults();
    }
    message_search_seq_ = 0;
    if (message_search_timeout_timer_ != nullptr) {
        message_search_timeout_timer_->stop();
    }
    if (message_search_dialog_ != nullptr) {
        message_search_dialog_->close();
        message_search_dialog_->clearResults();
    }
    clearReply();
    QSettings settings;
    settings.remove("session");
    settings.sync();
    if (conversation_list_ != nullptr) {
        conversation_list_->clear();
    }
    if (room_list_ != nullptr) {
        room_list_->clear();
    }
    updateOwnProfileStrip();
    refreshUnreadTitle();
    resetContext();
    setOnline(false, "未连接");
    stack_->setCurrentWidget(login_page_);
    login_page_->setBusy(false);
    login_page_->clearPassword();
    login_page_->setStatus("会话已失效，请重新登录", true);
}

void MainWindow::resetContext() {
    context_type_ = ContextType::None;
    current_room_id_ = 0;
    current_peer_id_ = 0;
    current_peer_name_.clear();
    message_store_.reset(chatbox::client::ChatContext::kRoom, 0);
    has_more_ = false;
    loading_older_history_ = false;
    next_before_id_ = 0;
    if (input_edit_ != nullptr) {
        input_edit_->clear();
    }
    if (mention_button_ != nullptr) {
        mention_button_->hide();
    }
    clearReply();
    if (context_label_ != nullptr) {
        context_label_->setText("请选择房间或私聊");
    }
    if (context_avatar_ != nullptr) {
        context_avatar_->setPixmap(avatarIcon("C", 0).pixmap(32, 32));
        context_avatar_->setCursor(Qt::ArrowCursor);
    }
    if (chat_view_ != nullptr) {
        chat_view_->clear();
    chat_view_->clearHoverState();
    hideMessageActionBar();
    }
    if (member_list_ != nullptr) {
        member_list_->clear();
    }
    if (load_more_action_ != nullptr) {
        load_more_action_->setEnabled(false);
    }
}

void MainWindow::renderCurrentConversation(ScrollMode mode) {
    QScrollBar* bar = chat_view_->verticalScrollBar();
    const int previous = bar->value();
    hideMessageActionBar();
    QVector<chatbox::client::MessageKey> rendered_messages;
    chat_view_->setConversationHtml(
        chatbox::client::renderConversationHtml(message_store_.messages(), user_id_,
                                                 &rendered_messages),
        rendered_messages);
    switch (mode) {
        case ScrollMode::kBottom:
            bar->setValue(bar->maximum());
            break;
        case ScrollMode::kTop:
            bar->setValue(0);
            break;
        case ScrollMode::kPreserve:
            bar->setValue(std::min(previous, bar->maximum()));
            break;
    }
}

void MainWindow::addIncomingMessage(const chatbox::client::ChatMessage& message) {
    if (!message_store_.isCurrent(message.context, message.target_id)) {
        return;
    }
    QScrollBar* bar = chat_view_->verticalScrollBar();
    const bool at_bottom = bar->value() >= bar->maximum() - 8;
    message_store_.appendIncoming(message);
    renderCurrentConversation(at_bottom ? ScrollMode::kBottom : ScrollMode::kPreserve);
}

bool MainWindow::currentChatContext(chatbox::client::ChatContext& context,
                                    quint64& target_id) const {
    if (context_type_ == ContextType::Room) {
        context = chatbox::client::ChatContext::kRoom;
        target_id = static_cast<quint64>(current_room_id_);
        return true;
    }
    if (context_type_ == ContextType::Private) {
        context = chatbox::client::ChatContext::kPrivate;
        target_id = current_peer_id_;
        return true;
    }
    return false;
}

void MainWindow::setStatus(const QString& text, bool error) {
    if (status_label_ == nullptr) {
        return;
    }
    status_label_->setText(text);
    status_label_->setStyleSheet(error
                                     ? QString("color: %1;").arg(theme::kSemanticDown)
                                     : QString("color: %1;").arg(theme::kBody));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == context_avatar_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton &&
            context_type_ == ContextType::Private && current_peer_id_ != 0) {
            openProfile(current_peer_id_, current_peer_name_);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    shutting_down_ = true;
    if (network_ != nullptr) {
        network_->requestStop();
        network_->wait();
    }
    event->accept();
}
