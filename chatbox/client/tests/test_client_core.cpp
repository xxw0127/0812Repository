#include <QtTest>
#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QToolButton>
#include <QVector>

#include "core/app_settings.h"
#include "core/autostart.h"
#include "core/client_policy.h"
#include "core/credential_store.h"
#include "core/avatar_cache.h"
#include "core/avatar_utils.h"
#include "core/contact_store.h"
#include "core/history_store.h"
#include "core/identity.h"
#include "core/message_renderer.h"
#include "core/message_store.h"
#include "core/message_time.h"
#include "core/outbox.h"
#include "core/ui_theme.h"
#include "core/user_profile.h"
#include "ui/chat_view.h"
#include "ui/login_page.h"
#include "ui/message_action_bar.h"
#include "ui/profile_dialog.h"
#include "ui/settings_dialog.h"

namespace {

QString readClientSource(const QString& relative) {
    QString path = QCoreApplication::applicationDirPath() +
                   QStringLiteral("/../../") + relative;
    if (!QFile::exists(path)) {
        path = QStringLiteral("../") + relative;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString sourceSection(const QString& source, const QString& marker,
                      const QString& next_marker) {
    const int start = source.indexOf(marker);
    if (start < 0) {
        return QString();
    }
    const int end = source.indexOf(next_marker, start + marker.size());
    return end < 0 ? source.mid(start) : source.mid(start, end - start);
}

QString caseSection(const QString& source, const QString& label) {
    return sourceSection(source, label, QStringLiteral("\n        case chatbox::"));
}

QString functionSection(const QString& source, const QString& signature) {
    return sourceSection(source, signature, QStringLiteral("\nvoid MainWindow::"));
}

// 构造一条服务端已确认的消息（message_id > 0）。
chatbox::client::ChatMessage confirmedMessage(quint64 id, quint64 target,
                                              const QString& content,
                                              const QString& utc = QStringLiteral("2026-10-20 10:00:00")) {
    chatbox::client::ChatMessage message;
    message.message_id = id;
    message.context = chatbox::client::ChatContext::kRoom;
    message.target_id = target;
    message.sender_id = 9001;
    message.sender_name = QStringLiteral("alice");
    message.content = content;
    message.created_at_utc = chatbox::client::parseServerUtc(utc);
    message.status = chatbox::client::MessageStatus::kSent;
    return message;
}

}  // namespace

class TestClientCore : public QObject {
    Q_OBJECT

private slots:
    void parseServerUtc();
    void formatLocalTimeAndDate();
    void invalidTimestamp();
    void publicUserIdFormat();
    void messageStorePendingAndAck();
    void messageStoreHistoryPreservesPending();
    void messageStorePrependHistory();
    void messageStoreReconcilesLapsedAck();
    void messageStoreMarkRecalled();
    void messageStoreMergesLocalAndServerPagesInOrder();
    void messageStoreWritesThroughWhenHistoryAttached();
    void outboxExpiresAndMarksFailed();
    void outboxMarksSentAndAllFailed();
    void rendererEscapesHtml();
    void rendererShowsRetryOnlyWhenFailed();
    void rendererAddsDateSeparator();
    void rendererAlignsOwnMessages();
    void rendererQuoteBlockWithoutInlineActions();
    void messageActionsAvailability();
    void messageActionsRecallWindow();
    void chatViewMapsMessagesToIds();
    void chatViewStylesRetryLink();
    void chatViewHoverReentersSameMessage();
    void contactStorePersistsAndUpdates();
    void avatarCenterCrop();
    void avatarCompressionLimit();
    void avatarFallback();
    void avatarCacheVersion();
    void avatarCacheIgnoresOlderVersions();
    void profileDisplayNameFallsBackToNickname();
    void profileDialogSelfMode();
    void profileDialogViewModeActions();
    void messageBubbleColoursComeFromTheme();
    void themeTokensMatchStyleSheet();
    void messageActionBarHasSingleSoftShadow();
    void brandAppIconExposesSizes();
    void roomAvatarIsBrandBadge();
    void trayIconUsesBrandIcon();
    void memberListShowsAvatarFromCache();
    void fallbackAvatarPaletteIsSingleSource();
    void appTitleUsesBrandName();
    void settingsMigrationCopiesLegacyStore();
    void settingsMigrationNeverOverwritesCurrent();
    void brandIdentifiersAreUnified();
    void installerTemplateTargetsChatboxExe();
    void privateChatHasNoMentionEntry();
    void privatePushSkipsMentionNotification();
    void headerUsesAvatarAndMoreMenu();
    void systemPopupNotificationIsOptional();
    void autostartCommandCarriesMinimizedFlag();
    void autostartSettingWritesAndRemovesRunValue();
    void startupHonoursMinimizedAndSingleInstance();
    void startupPolicyRequiresUsableTray();
    void unreadBadgesHonourAlertsSwitch();
    void historyStoreCreatesSchema();
    void historyStoreReportsOpenFailure();
    void historyStoreRejectsSecondInstance();
    void historyStoreSaveAndLoadRecent();
    void historyStoreDedupesByMessageId();
    void historyStoreSkipsUnconfirmedMessages();
    void historyStoreMarksRecalled();
    void historyStoreRecallIsConversationScoped();
    void historyStoreIsolatesUsers();
    void historyStoreIsolatesConversations();
    void historyStoreSearchFindsInConversation();
    void historyStoreConversationSummaries();
    void historyStoreClearAllAndOlderThan();
    void historyStorePathMoveCopiesData();
    void historyStoreRejectsExistingTarget();
    void buildScriptShipsSqlDriver();
    void conversationOpensFromLocalHistoryFirst();
    void incomingPushesArePersistedForOtherConversations();
    void recallPushUpdatesBackgroundHistory();
    void loginPageHasNoLocalHistoryEntry();
    void loginPageHidesServerFieldsBehindMoreDots();
    void loginPageShowsNicknameOnlyInRegisterMode();
    void loginPageHasAutoLoginAndRememberPasswordOptions();
    void loginPageReloadsRememberedPasswordAfterLogout();
    void credentialStoreEncryptsSavedPassword();
    void settingsDialogDefaultsToPublicServer();
    void offlineModeIsReadOnlyAndLocal();
    void settingsDialogShowsHistoryInfo();
    void settingsDialogExposesHistoryActions();
};

void TestClientCore::parseServerUtc() {
    const QDateTime parsed = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.timeSpec(), Qt::UTC);
    QCOMPARE(parsed.date(), QDate(2026, 10, 18));
    QCOMPARE(parsed.time(), QTime(12, 0, 0));
}

void TestClientCore::formatLocalTimeAndDate() {
    const QDateTime local(QDate(2026, 10, 18), QTime(10, 30, 0), Qt::LocalTime);
    const QDateTime utc = local.toUTC();
    QCOMPARE(chatbox::client::formatLocalTime(utc), QString("10:30"));
    QCOMPARE(chatbox::client::formatLocalDate(utc), QString("2026-10-18"));
}

void TestClientCore::invalidTimestamp() {
    const QDateTime parsed = chatbox::client::parseServerUtc("not-a-time");
    QVERIFY(!parsed.isValid());
    QCOMPARE(chatbox::client::formatLocalTime(parsed), QString("--:--"));
    QCOMPARE(chatbox::client::formatLocalDate(parsed), QString("--"));
}

void TestClientCore::publicUserIdFormat() {
    QCOMPARE(chatbox::client::publicUserId(278), QString("U00000278"));
    QCOMPARE(chatbox::client::publicUserId(123456789), QString("U123456789"));
    QVERIFY(chatbox::client::publicUserId(278) !=
            chatbox::client::publicUserId(279));
}

void TestClientCore::messageStorePendingAndAck() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kPrivate, 7);

    chatbox::client::ChatMessage pending;
    pending.client_msg_id = "p1";
    pending.content = "hello";
    pending.status = chatbox::client::MessageStatus::kSending;
    store.addPending(pending);

    QCOMPARE(store.messages().size(), 1);
    QVERIFY(store.findByClientId("p1") != nullptr);
    QCOMPARE(store.findByClientId("p1")->content, QString("hello"));
    QVERIFY(store.findByClientId("missing") == nullptr);
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    QVERIFY(store.markSent("p1", 42, created));
    QCOMPARE(store.messages().size(), 1);
    QCOMPARE(store.messages().at(0).message_id, quint64(42));
    QCOMPARE(store.messages().at(0).status, chatbox::client::MessageStatus::kSent);

    QVERIFY(store.markSent("p1", 42, created));
    QCOMPARE(store.messages().size(), 1);

    QVERIFY(store.markSending("p1"));
    QCOMPARE(store.messages().at(0).status, chatbox::client::MessageStatus::kSending);

    store.addPending(pending);
    QCOMPARE(store.messages().size(), 1);
}

void TestClientCore::messageStoreHistoryPreservesPending() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 1);

    chatbox::client::ChatMessage pending;
    pending.client_msg_id = "p1";
    pending.status = chatbox::client::MessageStatus::kSending;
    pending.content = "pending";
    store.addPending(pending);

    chatbox::client::ChatMessage history;
    history.message_id = 1;
    history.status = chatbox::client::MessageStatus::kSent;
    history.content = "history";
    store.replaceHistory({history});

    QCOMPARE(store.messages().size(), 2);
    QCOMPARE(store.messages().at(0).message_id, quint64(1));
    QCOMPARE(store.messages().at(1).client_msg_id, QString("p1"));
    QCOMPARE(store.messages().at(1).status, chatbox::client::MessageStatus::kSending);
}

void TestClientCore::messageStorePrependHistory() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 1);

    chatbox::client::ChatMessage current;
    current.message_id = 10;
    current.status = chatbox::client::MessageStatus::kSent;
    store.appendIncoming(current);

    chatbox::client::ChatMessage older8;
    older8.message_id = 8;
    older8.status = chatbox::client::MessageStatus::kSent;
    chatbox::client::ChatMessage older9;
    older9.message_id = 9;
    older9.status = chatbox::client::MessageStatus::kSent;
    store.prependHistory({older8, older9});
    QCOMPARE(store.messages().size(), 3);
    QCOMPARE(store.messages().at(0).message_id, quint64(8));
    QCOMPARE(store.messages().at(1).message_id, quint64(9));
    QCOMPARE(store.messages().at(2).message_id, quint64(10));

    store.prependHistory({older8, older9});
    QCOMPARE(store.messages().size(), 3);
}

void TestClientCore::messageStoreReconcilesLapsedAck() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kPrivate, 7);

    const QDateTime sent_at = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage pending;
    pending.client_msg_id = "p1";
    pending.context = chatbox::client::ChatContext::kPrivate;
    pending.target_id = 7;
    pending.sender_id = 1;
    pending.content = "hello";
    pending.created_at_utc = sent_at;
    pending.outgoing = true;
    pending.status = chatbox::client::MessageStatus::kFailed;
    store.addPending(pending);

    chatbox::client::ChatMessage history;
    history.message_id = 42;
    history.context = chatbox::client::ChatContext::kPrivate;
    history.target_id = 7;
    history.sender_id = 1;
    history.content = "hello";
    history.created_at_utc = sent_at.addSecs(1);
    history.outgoing = true;
    history.status = chatbox::client::MessageStatus::kSent;
    store.replaceHistory({history});

    QCOMPARE(store.messages().size(), 1);
    QCOMPARE(store.messages().at(0).message_id, quint64(42));
    QCOMPARE(store.messages().at(0).client_msg_id, QString("p1"));

    QVERIFY(store.markSent("p1", 42, sent_at.addSecs(1)));
    QCOMPARE(store.messages().size(), 1);
}

void TestClientCore::messageStoreMarkRecalled() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 1);
    chatbox::client::ChatMessage message;
    message.message_id = 99;
    message.content = "to recall";
    message.status = chatbox::client::MessageStatus::kSent;
    store.appendIncoming(message);

    QVERIFY(store.findMessage(99) != nullptr);
    QVERIFY(store.markRecalled(99));
    QCOMPARE(store.messages().at(0).recalled, true);
    QVERIFY(store.messages().at(0).content.isEmpty());
    QVERIFY(!store.markRecalled(100));
}

// 本地首屏与服务端返回的那一页必须合并成一条按时间递增的序列：
// 服务端页里有更新的消息时，本地更旧的行不能被留在最底下。
void TestClientCore::messageStoreMergesLocalAndServerPagesInOrder() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 7);
    QVector<chatbox::client::ChatMessage> local;
    for (quint64 id = 1; id <= 10; ++id) {
        local.push_back(confirmedMessage(id, 7, "local"));
    }
    store.replaceHistory(local);
    QVector<chatbox::client::ChatMessage> server;
    for (quint64 id = 6; id <= 12; ++id) {
        server.push_back(confirmedMessage(id, 7, "server"));
    }
    store.replaceHistory(server);
    const auto& merged = store.messages();
    QCOMPARE(merged.size(), 12);
    for (int i = 0; i < merged.size(); ++i) {
        QCOMPARE(merged.at(i).message_id, quint64(i + 1));
    }
    chatbox::client::ChatMessage pending;
    pending.client_msg_id = QStringLiteral("pending-order");
    pending.status = chatbox::client::MessageStatus::kSending;
    store.addPending(pending);
    QCOMPARE(store.messages().last().message_id, quint64(0));
}

void TestClientCore::messageStoreWritesThroughWhenHistoryAttached() {
    QTemporaryDir dir; chatbox::client::HistoryStore history; QString error;
    QVERIFY(history.open(dir.path(), 1401, &error));

    chatbox::client::MessageStore store;
    store.setHistoryStore(&history);
    // 撤回是按会话定位的：store 必须先知道自己管的是哪个会话。
    store.reset(chatbox::client::ChatContext::kRoom, 7);
    QVector<chatbox::client::ChatMessage> page;
    page << confirmedMessage(1, 7, "first") << confirmedMessage(2, 7, "second");
    store.replaceHistory(page);
    store.appendIncoming(confirmedMessage(3, 7, "push"));
    QCOMPARE(history.messageCount(), qint64(3));

    chatbox::client::ChatMessage pending = confirmedMessage(0, 7, "local");
    pending.client_msg_id = QStringLiteral("client-1");
    pending.status = chatbox::client::MessageStatus::kSending;
    store.addPending(pending);
    store.markFailed(pending.client_msg_id);
    QCOMPARE(history.messageCount(), qint64(3));  // 未确认消息不落盘

    chatbox::client::ChatMessage outgoing = confirmedMessage(0, 7, "sent");
    outgoing.client_msg_id = QStringLiteral("client-2");
    outgoing.status = chatbox::client::MessageStatus::kSending;
    store.addPending(outgoing);
    QVERIFY(store.markSent(outgoing.client_msg_id, 4, QDateTime::currentDateTimeUtc()));
    QCOMPARE(history.messageCount(), qint64(4));

    QVERIFY(store.markRecalled(4));
    const auto rows = history.recent(chatbox::client::ChatContext::kRoom, 7, 10);
    QVERIFY(rows.last().recalled);
    QVERIFY(rows.last().content.isEmpty());

    store.setHistoryStore(nullptr);
    store.appendIncoming(confirmedMessage(5, 7, "detached"));
    QCOMPARE(history.messageCount(), qint64(4));
}

void TestClientCore::outboxExpiresAndMarksFailed() {
    chatbox::client::Outbox outbox;
    const QDateTime t0 = chatbox::client::parseServerUtc("2026-10-18 12:00:00");

    chatbox::client::ChatMessage message;
    message.client_msg_id = "p1";
    message.status = chatbox::client::MessageStatus::kSending;
    outbox.add(message, t0);
    QVERIFY(outbox.contains("p1"));

    QVERIFY(outbox.expireSending(t0.addSecs(5), 10).isEmpty());
    const QVector<QString> expired = outbox.expireSending(t0.addSecs(11), 10);
    QCOMPARE(expired.size(), 1);
    QCOMPARE(expired.at(0), QString("p1"));
    QCOMPARE(outbox.pending().at(0).status, chatbox::client::MessageStatus::kFailed);

    QVERIFY(outbox.markSent("p1"));
    QVERIFY(!outbox.contains("p1"));
}

void TestClientCore::outboxMarksSentAndAllFailed() {
    chatbox::client::Outbox outbox;
    const QDateTime now = chatbox::client::parseServerUtc("2026-10-18 12:00:00");

    chatbox::client::ChatMessage first;
    first.client_msg_id = "p1";
    first.status = chatbox::client::MessageStatus::kSending;
    chatbox::client::ChatMessage second;
    second.client_msg_id = "p2";
    second.status = chatbox::client::MessageStatus::kSending;
    outbox.add(first, now);
    outbox.add(second, now);

    QVERIFY(outbox.markSent("p1"));
    QVERIFY(!outbox.contains("p1"));
    QCOMPARE(outbox.markAllSendingFailed(), 1);
    QCOMPARE(outbox.pending().at(0).status, chatbox::client::MessageStatus::kFailed);
}

void TestClientCore::rendererEscapesHtml() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 1;
    message.sender_name = "<b>Alice</b>";
    message.content = "<script>alert(\"x\")</script>";
    message.created_at_utc = created;
    message.status = chatbox::client::MessageStatus::kSent;

    const QString html = chatbox::client::renderConversationHtml({message}, 99);
    QVERIFY(html.contains("&lt;script&gt;"));
    QVERIFY(html.contains("&lt;b&gt;Alice&lt;/b&gt;"));
    QVERIFY(!html.contains("<script>"));
    QVERIFY(!html.contains("<b>Alice</b>"));
}

void TestClientCore::rendererShowsRetryOnlyWhenFailed() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p1";
    failed.content = "hello";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;

    QString html = chatbox::client::renderConversationHtml({failed}, 1);
    QVERIFY(html.contains("retry:p1"));
    QVERIFY(html.contains("发送失败"));

    chatbox::client::ChatMessage sent = failed;
    sent.status = chatbox::client::MessageStatus::kSent;
    html = chatbox::client::renderConversationHtml({sent}, 1);
    QVERIFY(!html.contains("retry:p1"));
    QVERIFY(!html.contains("发送失败"));
}

void TestClientCore::rendererAddsDateSeparator() {
    const QDateTime first_local(QDate(2026, 10, 18), QTime(10, 0), Qt::LocalTime);
    const QDateTime second_local(QDate(2026, 10, 19), QTime(10, 0), Qt::LocalTime);

    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.content = "day one";
    first.created_at_utc = first_local.toUTC();
    chatbox::client::ChatMessage second;
    second.message_id = 2;
    second.content = "day two";
    second.created_at_utc = second_local.toUTC();

    const QString html = chatbox::client::renderConversationHtml({first, second}, 1);
    QVERIFY(html.contains("2026-10-18"));
    QVERIFY(html.contains("2026-10-19"));
}

void TestClientCore::rendererAlignsOwnMessages() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage own;
    own.message_id = 1;
    own.content = "own";
    own.created_at_utc = created;
    own.outgoing = true;

    QString html = chatbox::client::renderConversationHtml({own}, 1);
    QVERIFY(html.contains("align=\"right\""));

    chatbox::client::ChatMessage other;
    other.message_id = 2;
    other.sender_id = 2;
    other.sender_name = "Bob";
    other.content = "other";
    other.created_at_utc = created;
    other.outgoing = false;

    html = chatbox::client::renderConversationHtml({other}, 1);
    QVERIFY(html.contains("align=\"left\""));
}

void TestClientCore::rendererQuoteBlockWithoutInlineActions() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage quoted;
    quoted.message_id = 7;
    quoted.sender_name = "Bob";
    quoted.content = "quoted body";
    quoted.created_at_utc = created;
    quoted.outgoing = false;
    quoted.reply_to_id = 5;
    quoted.reply_sender_name = "Alice";
    quoted.reply_content = "original text";

    QString html = chatbox::client::renderConversationHtml({quoted}, 1);
    QVERIFY(html.contains("回复 Alice: original text"));
    QVERIFY(!html.contains("quote:7"));

    chatbox::client::ChatMessage recalled = quoted;
    recalled.message_id = 8;
    recalled.outgoing = true;
    recalled.content = "secret";
    recalled.recalled = true;
    html = chatbox::client::renderConversationHtml({recalled}, 1);
    QVERIFY(html.contains("消息已撤回"));
    QVERIFY(!html.contains("secret"));
    QVERIFY(!html.contains("recall:8"));

    chatbox::client::ChatMessage own;
    own.message_id = 9;
    own.content = "own";
    own.created_at_utc = created;
    own.outgoing = true;
    html = chatbox::client::renderConversationHtml({own}, 1);
    QVERIFY(!html.contains("recall:9"));
}

void TestClientCore::messageActionsAvailability() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    chatbox::client::ChatMessage incoming;
    incoming.message_id = 5;
    incoming.sender_id = 2;
    chatbox::client::MessageActions actions =
        chatbox::client::messageActions(incoming, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(!actions.can_recall);
    QVERIFY(!actions.can_retry);

    chatbox::client::ChatMessage own;
    own.message_id = 6;
    own.outgoing = true;
    own.created_at_utc = now;
    actions = chatbox::client::messageActions(own, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(actions.can_recall);

    // Room history marks the user's own messages by sender id.
    chatbox::client::ChatMessage own_by_sender;
    own_by_sender.message_id = 7;
    own_by_sender.sender_id = 1;
    own_by_sender.created_at_utc = now.addSecs(-5);
    actions = chatbox::client::messageActions(own_by_sender, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(actions.can_recall);

    chatbox::client::ChatMessage recalled;
    recalled.message_id = 8;
    recalled.outgoing = true;
    recalled.recalled = true;
    recalled.created_at_utc = now;
    actions = chatbox::client::messageActions(recalled, 1, now);
    QVERIFY(!actions.can_reply);
    QVERIFY(!actions.can_recall);
    QVERIFY(!actions.can_retry);

    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p1";
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;
    actions = chatbox::client::messageActions(failed, 1, now);
    QVERIFY(actions.can_retry);
    QVERIFY(!actions.can_reply);
    QVERIFY(!actions.can_recall);

    chatbox::client::ChatMessage sending;
    sending.client_msg_id = "p2";
    sending.status = chatbox::client::MessageStatus::kSending;
    actions = chatbox::client::messageActions(sending, 1, now);
    QVERIFY(!actions.can_retry);
    QVERIFY(!actions.can_reply);
}

void TestClientCore::messageActionsRecallWindow() {
    // The client mirrors the server's 2-minute recall window so a stale
    // message never offers 撤回 that the server would reject.
    const QDateTime now = QDateTime::currentDateTimeUtc();

    chatbox::client::ChatMessage own;
    own.message_id = 9;
    own.outgoing = true;
    own.created_at_utc = now.addSecs(-121);
    chatbox::client::MessageActions actions =
        chatbox::client::messageActions(own, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(!actions.can_recall);

    own.created_at_utc = now.addSecs(-119);
    actions = chatbox::client::messageActions(own, 1, now);
    QVERIFY(actions.can_recall);

    // Exactly at the limit the server still accepts the recall.
    own.created_at_utc = now.addSecs(-120);
    actions = chatbox::client::messageActions(own, 1, now);
    QVERIFY(actions.can_recall);

    // Other people's messages never offer 撤回, however fresh they are.
    chatbox::client::ChatMessage incoming;
    incoming.message_id = 10;
    incoming.sender_id = 2;
    incoming.created_at_utc = now;
    actions = chatbox::client::messageActions(incoming, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(!actions.can_recall);

    // A message without a server timestamp cannot be recalled either.
    chatbox::client::ChatMessage no_time;
    no_time.message_id = 11;
    no_time.outgoing = true;
    actions = chatbox::client::messageActions(no_time, 1, now);
    QVERIFY(!actions.can_recall);

    // The default reference time is the current wall clock.
    own.created_at_utc = now.addSecs(-300);
    actions = chatbox::client::messageActions(own, 1);
    QVERIFY(actions.can_reply);
    QVERIFY(!actions.can_recall);
}

void TestClientCore::chatViewMapsMessagesToIds() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage first;
    first.message_id = 11;
    first.sender_id = 2;
    first.sender_name = "Bob";
    first.content = "first body";
    first.created_at_utc = created;
    chatbox::client::ChatMessage second;
    second.message_id = 22;
    second.sender_id = 1;
    second.sender_name = "Me";
    second.content = "second body";
    second.created_at_utc = created;
    second.outgoing = true;

    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p9";
    failed.content = "failed body";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;

    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({first, second, failed}, 1, &keys);
    QCOMPARE(keys.size(), 3);
    QCOMPARE(keys.at(0).message_id, quint64(11));
    QCOMPARE(keys.at(1).message_id, quint64(22));
    QCOMPARE(keys.at(2).message_id, quint64(0));
    QCOMPARE(keys.at(2).client_msg_id, QString("p9"));

    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);

    const QTextCursor first_cursor = view.document()->find("first body");
    QVERIFY(!first_cursor.isNull());
    QCOMPARE(view.messageIdAt(view.cursorRect(first_cursor).center()), quint64(11));

    const QTextCursor second_cursor = view.document()->find("second body");
    QVERIFY(!second_cursor.isNull());
    QCOMPARE(view.messageIdAt(view.cursorRect(second_cursor).center()), quint64(22));

    // The date separator is not part of any message.
    const QTextCursor date_cursor = view.document()->find("2026-10-18");
    QVERIFY(!date_cursor.isNull());
    QCOMPARE(view.messageIdAt(view.cursorRect(date_cursor).center()), quint64(0));

    // A failed message has no server id but keeps its client id, so the
    // context menu can still offer 重发 for it.
    const QTextCursor failed_cursor = view.document()->find("failed body");
    QVERIFY(!failed_cursor.isNull());
    const QPoint failed_pos = view.cursorRect(failed_cursor).center();
    QCOMPARE(view.messageIdAt(failed_pos), quint64(0));
    chatbox::client::MessageKey failed_key;
    QVERIFY(view.messageKeyAt(failed_pos, &failed_key));
    QCOMPARE(failed_key.client_msg_id, QString("p9"));

    chatbox::client::MessageKey none;
    QVERIFY(!view.messageKeyAt(QPoint(1, 1), &none));

    const QRect first_anchor = view.messageAnchorRect(11);
    const QRect second_anchor = view.messageAnchorRect(22);
    QVERIFY(!first_anchor.isNull());
    QVERIFY(!second_anchor.isNull());
    QVERIFY(second_anchor.top() > first_anchor.top());
    QVERIFY(view.messageAnchorRect(9999).isNull());

    // Bubble rects are in viewport coordinates and contain their content.
    const QRect first_bubble = view.messageBubbleRect(11);
    const QRect second_bubble = view.messageBubbleRect(22);
    QVERIFY(!first_bubble.isNull());
    QVERIFY(!second_bubble.isNull());
    QVERIFY(first_bubble.contains(view.cursorRect(first_cursor).center()));
    QVERIFY(second_bubble.contains(view.cursorRect(second_cursor).center()));
    QVERIFY(view.messageBubbleRect(9999).isNull());

    // After scrolling, bubble rects must follow the viewport.
    view.resize(640, 160);
    QTest::qWait(50);
    QScrollBar* scroll = view.verticalScrollBar();
    scroll->setValue(scroll->maximum());
    QTest::qWait(50);
    const QPoint second_content = view.cursorRect(second_cursor).center();
    QVERIFY(second_content.y() >= 0);
    QVERIFY(second_content.y() < view.viewport()->height());
    QVERIFY(view.messageBubbleRect(22).contains(second_content));
}

void TestClientCore::chatViewStylesRetryLink() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p1";
    failed.content = "oops";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;

    QVector<chatbox::client::MessageKey> keys;
    const QString html = chatbox::client::renderConversationHtml({failed}, 1, &keys);
    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);

    bool inspected = false;
    for (QTextBlock block = view.document()->begin(); block.isValid();
         block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.text().contains(QString::fromUtf8("重发"))) {
                continue;
            }
            const QTextCharFormat format = fragment.charFormat();
            QVERIFY(format.isAnchor());
            QVERIFY(!format.fontUnderline());
            QCOMPARE(format.foreground().color(),
                     QColor(chatbox::client::theme::kAccent));
            inspected = true;
        }
    }
    QVERIFY(inspected);
}

void TestClientCore::chatViewHoverReentersSameMessage() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage own;
    own.message_id = 22;
    own.sender_id = 1;
    own.sender_name = "Me";
    own.content = "hover body";
    own.created_at_utc = created;
    own.outgoing = true;

    QVector<chatbox::client::MessageKey> keys;
    const QString html = chatbox::client::renderConversationHtml({own}, 1, &keys);

    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);

    const QTextCursor cursor = view.document()->find("hover body");
    QVERIFY(!cursor.isNull());
    const QPoint pos = view.cursorRect(cursor).center();
    QCOMPARE(view.messageIdAt(pos), quint64(22));

    int hovered = 0;
    int cleared = 0;
    QObject::connect(&view, &ChatView::hoveredMessage,
                     [&hovered](quint64, const QRect&) { ++hovered; });
    QObject::connect(&view, &ChatView::hoverCleared, [&cleared]() { ++cleared; });

    const auto send_move = [&view](const QPoint& point) {
        QMouseEvent event(QEvent::MouseMove, QPointF(point),
                          QPointF(view.viewport()->mapToGlobal(point)),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
    };

    send_move(pos);
    QCOMPARE(hovered, 1);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(view.viewport(), &leave);
    QCOMPARE(cleared, 1);

    // Returning to the same bubble after leaving the view must show the
    // action bar again; a stale hovered id used to suppress the signal.
    send_move(pos);
    QCOMPARE(hovered, 2);
}

void TestClientCore::contactStorePersistsAndUpdates() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QCoreApplication::setOrganizationName("ChatBoxTest");
    QCoreApplication::setApplicationName("ClientCoreTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings settings;
    settings.remove("contacts");
    settings.sync();

    chatbox::client::Contact first;
    first.user_id = 42;
    first.username = "alice";
    first.nickname = "Alice";
    chatbox::client::ContactStore store;
    QVERIFY(store.add(first));
    QCOMPARE(store.contacts().size(), 1);

    first.nickname = "Alice 2";
    QVERIFY(!store.add(first));
    QCOMPARE(store.contacts().size(), 1);
    QCOMPARE(store.contacts().at(0).nickname, QString("Alice 2"));

    chatbox::client::Contact second;
    second.user_id = 43;
    second.username = "bob";
    second.nickname = "Bob";
    QVERIFY(store.add(second));

    chatbox::client::ContactStore reloaded;
    QCOMPARE(reloaded.contacts().size(), 2);
    QVERIFY(reloaded.remove(42));
    QCOMPARE(reloaded.contacts().size(), 1);
    QCOMPARE(reloaded.contacts().at(0).user_id, quint64(43));

    chatbox::client::ContactStore other("other-scope");
    other.add(first);
    QCOMPARE(other.contacts().size(), 1);
    QCOMPARE(reloaded.contacts().size(), 1);

    settings.remove("contacts");
    settings.sync();
}

void TestClientCore::avatarCenterCrop() {
    QImage wide(2, 1, QImage::Format_RGB32);
    wide.fill(Qt::red);
    const QImage cropped = chatbox::client::centerCropSquare(wide);
    QVERIFY(!cropped.isNull());
    QCOMPARE(cropped.width(), 1);
    QCOMPARE(cropped.height(), 1);

    QImage square(5, 5, QImage::Format_RGB32);
    square.fill(Qt::blue);
    const QImage unchanged = chatbox::client::centerCropSquare(square);
    QCOMPARE(unchanged.size(), QSize(5, 5));

    QVERIFY(chatbox::client::centerCropSquare(QImage()).isNull());
}

void TestClientCore::avatarCompressionLimit() {
    QImage noisy(256, 256, QImage::Format_RGB32);
    quint32 state = 0x12345678u;
    for (int y = 0; y < noisy.height(); ++y) {
        for (int x = 0; x < noisy.width(); ++x) {
            state = state * 1664525u + 1013904223u;
            const int red = static_cast<int>((state >> 16) & 0xFF);
            state = state * 1664525u + 1013904223u;
            const int green = static_cast<int>((state >> 16) & 0xFF);
            state = state * 1664525u + 1013904223u;
            const int blue = static_cast<int>((state >> 16) & 0xFF);
            noisy.setPixel(x, y, qRgb(red, green, blue));
        }
    }

    const QByteArray jpeg = chatbox::client::encodeAvatarJpeg(noisy);
    QVERIFY(!jpeg.isEmpty());
    QVERIFY(jpeg.size() <= 48 * 1024);
    QVERIFY(jpeg.startsWith(QByteArray::fromHex("ffd8ff")));

    QVERIFY(chatbox::client::encodeAvatarJpeg(QImage()).isEmpty());
    QVERIFY(chatbox::client::encodeAvatarJpeg(noisy, 1).isEmpty());
}

void TestClientCore::avatarFallback() {
    const QImage fallback = chatbox::client::fallbackAvatar("Alice", 42, 64);
    QVERIFY(!fallback.isNull());
    QCOMPARE(fallback.size(), QSize(64, 64));
    QVERIFY(fallback.hasAlphaChannel());
    QVERIFY(fallback.pixelColor(32, 32).alpha() > 0);
    QCOMPARE(fallback.pixelColor(0, 0).alpha(), 0);

    const QImage same = chatbox::client::fallbackAvatar("Alice", 42, 64);
    QVERIFY(fallback == same);

    QVERIFY(!chatbox::client::fallbackAvatar(QString(), 42, 64).isNull());
    QVERIFY(chatbox::client::fallbackAvatar("Alice", 42, 0).isNull());
}

void TestClientCore::avatarCacheVersion() {
    chatbox::client::AvatarCache cache;
    QImage image;
    QVERIFY(!cache.needsFetch(7, 0));
    QVERIFY(cache.needsFetch(7, 3));
    QVERIFY(!cache.lookup(7, image));
    QVERIFY(!cache.contains(7));

    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::red);
    cache.store(7, 3, source);
    QVERIFY(cache.contains(7));
    QVERIFY(!cache.needsFetch(7, 3));
    QVERIFY(cache.lookup(7, image));
    QVERIFY(image == source);
    QVERIFY(cache.needsFetch(7, 4));

    cache.store(8, 1, QImage());
    QVERIFY(cache.contains(8));
    QVERIFY(!cache.needsFetch(8, 1));
    QVERIFY(!cache.lookup(8, image));

    cache.remove(7);
    QVERIFY(!cache.contains(7));
    QVERIFY(cache.needsFetch(7, 3));
    QVERIFY(cache.contains(8));

    cache.clear();
    QVERIFY(!cache.contains(8));
    QVERIFY(cache.needsFetch(7, 3));
    QVERIFY(!cache.lookup(7, image));
}

void TestClientCore::avatarCacheIgnoresOlderVersions() {
    chatbox::client::AvatarCache cache;
    QImage image;

    QImage version2(8, 8, QImage::Format_RGB32);
    version2.fill(Qt::green);
    QImage version1(8, 8, QImage::Format_RGB32);
    version1.fill(Qt::red);
    QImage version2_update(8, 8, QImage::Format_RGB32);
    version2_update.fill(Qt::blue);

    cache.store(11, 2, version2);
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version2);
    QVERIFY(!cache.needsFetch(11, 2));

    // A late response for version 1 must not overwrite version 2.
    cache.store(11, 1, version1);
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version2);
    QVERIFY(!cache.needsFetch(11, 2));

    // An update for the same version is accepted.
    cache.store(11, 2, version2_update);
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version2_update);

    // Version 0 is older than any loaded avatar version and is ignored.
    cache.store(11, 0, QImage());
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version2_update);

    // A newer version replaces the cached entry.
    cache.store(11, 3, version1);
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version1);

    // A newer explicit "no avatar" entry blocks a late older image.
    cache.store(12, 2, QImage());
    QVERIFY(!cache.lookup(12, image));
    cache.store(12, 1, version1);
    QVERIFY(!cache.lookup(12, image));
    QVERIFY(!cache.needsFetch(12, 2));
}

void TestClientCore::profileDisplayNameFallsBackToNickname() {
    chatbox::client::UserProfile profile;
    profile.nickname = QString::fromUtf8("昵称");
    QCOMPARE(chatbox::client::profileDisplayName(profile), QString::fromUtf8("昵称"));

    profile.remark = QString::fromUtf8("备注");
    QCOMPARE(chatbox::client::profileDisplayName(profile), QString::fromUtf8("备注"));

    profile.remark = QString::fromUtf8("  备注  ");
    QCOMPARE(chatbox::client::profileDisplayName(profile), QString::fromUtf8("备注"));

    profile.remark = "   ";
    QCOMPARE(chatbox::client::profileDisplayName(profile), QString::fromUtf8("昵称"));
}

void TestClientCore::profileDialogSelfMode() {
    ProfileDialog dialog;
    chatbox::client::UserProfile profile;
    profile.user_id = 278;
    profile.username = "alice";
    profile.nickname = QString::fromUtf8("爱丽丝");
    profile.signature.clear();
    profile.relationship = 0;

    dialog.setProfile(profile, true);
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.show();
    QVERIFY(dialog.isVisible());
    QVERIFY(dialog.width() >= 420);
    dialog.hide();

    auto* avatar = dialog.findChild<QLabel*>("ProfileAvatar");
    auto* name = dialog.findChild<QLabel*>("ProfileNameLabel");
    auto* username = dialog.findChild<QLabel*>("ProfileUsernameLabel");
    auto* uid = dialog.findChild<QLabel*>("ProfileUidLabel");
    auto* signature = dialog.findChild<QLabel*>("ProfileSignatureLabel");
    auto* nickname_edit = dialog.findChild<QLineEdit*>("ProfileNicknameEdit");
    auto* signature_edit = dialog.findChild<QPlainTextEdit*>("ProfileSignatureEdit");
    auto* save = dialog.findChild<QPushButton*>("ProfileSaveButton");
    auto* remove_avatar = dialog.findChild<QPushButton*>("ProfileRemoveAvatarButton");
    auto* message = dialog.findChild<QPushButton*>("ProfileMessageButton");
    auto* status = dialog.findChild<QLabel*>("ProfileStatusLabel");
    auto* nickname_count = dialog.findChild<QLabel*>("ProfileNicknameCountLabel");
    auto* signature_count = dialog.findChild<QLabel*>("ProfileSignatureCountLabel");
    QVERIFY(avatar != nullptr);
    QVERIFY(name != nullptr);
    QVERIFY(username != nullptr);
    QVERIFY(uid != nullptr);
    QVERIFY(signature != nullptr);
    QVERIFY(nickname_edit != nullptr);
    QVERIFY(signature_edit != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(remove_avatar != nullptr);
    QVERIFY(message != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(nickname_count != nullptr);
    QVERIFY(signature_count != nullptr);

    QCOMPARE(name->text(), QString::fromUtf8("爱丽丝"));
    QCOMPARE(username->text(), QString("@alice"));
    QCOMPARE(uid->text(), QString("U00000278"));
    QCOMPARE(signature->text(), QString::fromUtf8("这个人很懒，什么都没写"));
    const QPixmap avatar_pixmap = avatar->pixmap(Qt::ReturnByValue);
    QVERIFY(!avatar_pixmap.isNull());
    QCOMPARE(avatar_pixmap.size(), QSize(96, 96));
    QVERIFY(nickname_edit->isVisibleTo(&dialog));
    QVERIFY(signature_edit->isVisibleTo(&dialog));
    QVERIFY(save->isVisibleTo(&dialog));
    QVERIFY(!message->isVisibleTo(&dialog));
    QVERIFY(!remove_avatar->isEnabled());

    QSignalSpy save_spy(&dialog, &ProfileDialog::saveRequested);
    nickname_edit->setText(QString::fromUtf8("新昵称"));
    signature_edit->setPlainText(QString::fromUtf8("新的签名"));
    save->click();
    QCOMPARE(save_spy.count(), 1);
    QList<QVariant> args = save_spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString::fromUtf8("新昵称"));
    QCOMPARE(args.at(1).toString(), QString::fromUtf8("新的签名"));
    QCOMPARE(args.at(2).toInt(), 0);
    QVERIFY(args.at(3).toByteArray().isEmpty());

    QImage image(12, 12, QImage::Format_RGB32);
    image.fill(Qt::red);
    dialog.setAvatar(image);
    QVERIFY(remove_avatar->isEnabled());
    remove_avatar->click();
    save->click();
    QCOMPARE(save_spy.count(), 1);
    args = save_spy.takeFirst();
    QCOMPARE(args.at(2).toInt(), 2);
    QVERIFY(args.at(3).toByteArray().isEmpty());

    nickname_edit->setText("   ");
    save->click();
    QCOMPARE(save_spy.count(), 0);
    QVERIFY(status->text().contains(QString::fromUtf8("昵称不能为空")));

    nickname_edit->setText(QString(33, QChar(0x4E00)));
    save->click();
    QCOMPARE(save_spy.count(), 0);
    QVERIFY(status->text().contains(QString::fromUtf8("32")));

    nickname_edit->setText(QString::fromUtf8("昵称"));
    signature_edit->setPlainText(QString(129, QChar('b')));
    save->click();
    QCOMPARE(save_spy.count(), 0);
    QVERIFY(status->text().contains(QString::fromUtf8("128")));

    // Counters and save both use trimmed text, so trailing whitespace does
    // not turn an otherwise valid boundary value into an error.
    dialog.setProfile(profile, true);
    const QString padded_nickname = QString(32, QChar('a')) + "   ";
    const QString padded_signature = QString(128, QChar('b')) + "   ";
    nickname_edit->setText(padded_nickname);
    signature_edit->setPlainText(padded_signature);
    QCOMPARE(nickname_count->text(), QString("32/32"));
    QCOMPARE(signature_count->text(), QString("128/128"));
    save->click();
    QCOMPARE(save_spy.count(), 1);
    args = save_spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString(32, QChar('a')));
    QCOMPARE(args.at(1).toString(), QString(128, QChar('b')));
    QCOMPARE(args.at(2).toInt(), 0);

    // 32 emoji are 32 code points but 64 UTF-16 units: they must be accepted
    // when paired with a 128-code-point signature.
    QVector<uint> emoji_points;
    for (int index = 0; index < 32; ++index) {
        emoji_points.append(0x1F600);
    }
    const QString emoji_nickname =
        QString::fromUcs4(emoji_points.constData(), emoji_points.size());
    dialog.setProfile(profile, true);
    nickname_edit->setText(emoji_nickname);
    signature_edit->setPlainText(QString(128, QChar('b')));
    save->click();
    QCOMPARE(save_spy.count(), 1);
    args = save_spy.takeFirst();
    QCOMPARE(args.at(0).toString(), emoji_nickname);
    QCOMPARE(args.at(1).toString(), QString(128, QChar('b')));
    QCOMPARE(args.at(2).toInt(), 0);

    emoji_points.append(0x1F600);
    nickname_edit->setText(
        QString::fromUcs4(emoji_points.constData(), emoji_points.size()));
    save->click();
    QCOMPARE(save_spy.count(), 0);

    dialog.setBusy(true);
    QVERIFY(!save->isEnabled());
    QVERIFY(!remove_avatar->isEnabled());
    QVERIFY(!nickname_edit->isEnabled());
    QVERIFY(!signature_edit->isEnabled());
    dialog.setBusy(false);
    QVERIFY(save->isEnabled());
    QVERIFY(nickname_edit->isEnabled());
    QVERIFY(signature_edit->isEnabled());
}

void TestClientCore::profileDialogViewModeActions() {
    ProfileDialog dialog;
    chatbox::client::UserProfile profile;
    profile.user_id = 42;
    profile.username = "bob";
    profile.nickname = QString::fromUtf8("鲍勃");
    profile.remark = QString::fromUtf8("老鲍");
    profile.online = true;
    profile.relationship = 1;

    dialog.setProfile(profile, false);

    auto* name = dialog.findChild<QLabel*>("ProfileNameLabel");
    auto* secondary = dialog.findChild<QLabel*>("ProfileNicknameLabel");
    auto* online = dialog.findChild<QLabel*>("ProfileOnlineLabel");
    auto* nickname_edit = dialog.findChild<QLineEdit*>("ProfileNicknameEdit");
    auto* save = dialog.findChild<QPushButton*>("ProfileSaveButton");
    auto* message = dialog.findChild<QPushButton*>("ProfileMessageButton");
    auto* add = dialog.findChild<QPushButton*>("ProfileAddFriendButton");
    auto* remark = dialog.findChild<QPushButton*>("ProfileRemarkButton");
    auto* remove = dialog.findChild<QPushButton*>("ProfileRemoveFriendButton");
    QVERIFY(name != nullptr);
    QVERIFY(secondary != nullptr);
    QVERIFY(online != nullptr);
    QVERIFY(nickname_edit != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(message != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(remark != nullptr);
    QVERIFY(remove != nullptr);

    QCOMPARE(name->text(), QString::fromUtf8("老鲍"));
    QVERIFY(secondary->isVisibleTo(&dialog));
    QVERIFY(secondary->text().contains(QString::fromUtf8("鲍勃")));
    QVERIFY(online->text().contains(QString::fromUtf8("在线")));
    QVERIFY(!nickname_edit->isVisibleTo(&dialog));
    QVERIFY(!save->isVisibleTo(&dialog));
    QVERIFY(message->isVisibleTo(&dialog));
    QVERIFY(remark->isVisibleTo(&dialog));
    QVERIFY(remove->isVisibleTo(&dialog));
    QVERIFY(!add->isVisibleTo(&dialog));

    QSignalSpy message_spy(&dialog, &ProfileDialog::messageRequested);
    QSignalSpy remark_spy(&dialog, &ProfileDialog::remarkRequested);
    QSignalSpy remove_spy(&dialog, &ProfileDialog::removeFriendRequested);
    message->click();
    remark->click();
    remove->click();
    QCOMPARE(message_spy.count(), 1);
    QCOMPARE(remark_spy.count(), 1);
    QCOMPARE(remove_spy.count(), 1);
    QCOMPARE(message_spy.at(0).at(0).toULongLong(), qulonglong(42));
    QCOMPARE(remark_spy.at(0).at(0).toULongLong(), qulonglong(42));
    QCOMPARE(remove_spy.at(0).at(0).toULongLong(), qulonglong(42));

    profile.relationship = 2;
    profile.remark.clear();
    profile.nickname = "Carol";
    profile.online = false;
    dialog.setProfile(profile, false);
    QCOMPARE(name->text(), QString("Carol"));
    QVERIFY(!secondary->isVisibleTo(&dialog));
    QVERIFY(online->text().contains(QString::fromUtf8("离线")));
    QVERIFY(add->isVisibleTo(&dialog));
    QVERIFY(!message->isVisibleTo(&dialog));

    QSignalSpy add_spy(&dialog, &ProfileDialog::addFriendRequested);
    add->click();
    QCOMPARE(add_spy.count(), 1);
    QCOMPARE(add_spy.at(0).at(0).toULongLong(), qulonglong(42));

    auto* refresh = dialog.findChild<QPushButton*>("ProfileRefreshButton");
    QVERIFY(refresh != nullptr);
    QVERIFY(refresh->isVisibleTo(&dialog));
    QSignalSpy refresh_spy(&dialog, &ProfileDialog::refreshRequested);
    refresh->click();
    QCOMPARE(refresh_spy.count(), 1);
    QCOMPARE(refresh_spy.at(0).at(0).toULongLong(), qulonglong(42));

    profile.relationship = 3;
    dialog.setProfile(profile, false);
    auto* pending = dialog.findChild<QPushButton*>("ProfilePendingButton");
    QVERIFY(pending != nullptr);
    QVERIFY(pending->isVisibleTo(&dialog));
    QCOMPARE(pending->text(), QString::fromUtf8("申请已发送"));
    QVERIFY(!pending->isEnabled());

    pending->setToolTip("stale");
    dialog.setProfile(profile, false);
    QVERIFY(pending->toolTip().isEmpty());

    profile.relationship = 4;
    dialog.setProfile(profile, false);
    QVERIFY(pending->isVisibleTo(&dialog));
    QCOMPARE(pending->text(), QString::fromUtf8("查看好友申请"));
    QVERIFY(pending->isEnabled());
    QVERIFY(pending->toolTip().isEmpty());
    QSignalSpy friend_requests_spy(&dialog,
                                   &ProfileDialog::friendRequestsRequested);
    pending->click();
    QCOMPARE(friend_requests_spy.count(), 1);

    dialog.setBusy(true);
    QVERIFY(!add->isEnabled());
    QVERIFY(!pending->isEnabled());
    QVERIFY(!dialog.findChild<QPushButton*>("ProfileRefreshButton")->isEnabled());
    dialog.setBusy(false);
    QVERIFY(pending->isEnabled());
}

// the conversation HTML must use the theme bubble palette (own messages keep
// the green semantic tint), not the retired WhatsApp-era greys.
void TestClientCore::messageBubbleColoursComeFromTheme() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-19 12:00:00");
    chatbox::client::ChatMessage own;
    own.client_msg_id = "c1";
    own.message_id = 1;
    own.sender_id = 1;
    own.content = "hi";
    own.created_at_utc = created;
    own.outgoing = true;

    chatbox::client::ChatMessage other;
    other.client_msg_id = "c2";
    other.message_id = 2;
    other.sender_id = 9;
    other.sender_name = "peer";
    other.content = "yo";
    other.created_at_utc = created;

    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "c3";
    failed.sender_id = 1;
    failed.content = "nope";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;

    const QString html = chatbox::client::renderConversationHtml({own, other, failed}, 1);
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kBubbleOwn)));
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kBubbleOther)));
    QVERIFY(!html.contains(QStringLiteral("#DCF8C6")));
    QVERIFY(!html.contains(QStringLiteral("#F2F2F2")));
    // a failed send is an error state, so it uses the semantic-down colour
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kSemanticDown)));
    QVERIFY(html.contains(QStringLiteral("retry:c3")));
}

// guards the palette in resources/style.qss (embedded as :/style.qss at build
// time) against drifting from core/ui_theme.h; bubble, banner and semantic
// colours live in C++/HTML only and are not asserted here.
void TestClientCore::themeTokensMatchStyleSheet() {
    // resolve relative to the test binary first so the guard also works when
    // the binary is launched outside tests/ (Qt Creator, CI, ...)
    QString style_path = QCoreApplication::applicationDirPath() +
                         QStringLiteral("/../../resources/style.qss");
    if (!QFile::exists(style_path)) {
        style_path = QStringLiteral("../resources/style.qss");
    }
    QFile style_file(style_path);
    QVERIFY2(style_file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(style_path + ": " + style_file.errorString()));
    const QString css = QString::fromUtf8(style_file.readAll()).toLower();

    // QSS 负责的颜色（App 从 :/style.qss 加载同一文件，构建时嵌入）。
    // 注意：kBody/kBubbleOwn/kBubbleOther/kSemanticUp/kSemanticDown/kBanner*
    // 只出现在 C++/HTML 里，不在 QSS，故不在此列表。
    const QStringList required = {
        chatbox::client::theme::kAccent,
        chatbox::client::theme::kAccentHover,
        chatbox::client::theme::kAccentActive,
        chatbox::client::theme::kAccentDisabled,
        chatbox::client::theme::kCanvas,
        chatbox::client::theme::kSurfaceSoft,
        chatbox::client::theme::kSurfaceStrong,
        chatbox::client::theme::kSurfaceStrongHover,
        chatbox::client::theme::kHairline,
        chatbox::client::theme::kInk,
        chatbox::client::theme::kMuted,
        chatbox::client::theme::kMutedSoft,
    };
    QStringList allowed;
    for (const QString& token : required) {
        QVERIFY2(css.contains(token.toLower()), qPrintable(token));
        allowed.append(token.toLower());
    }

    // every colour in the sheet must be one of the palette tokens above, so a
    // one-off hard-coded grey cannot sneak in unnoticed
    QRegularExpression colour(QStringLiteral("#[0-9a-f]{6}"));
    QRegularExpressionMatchIterator it = colour.globalMatch(css);
    while (it.hasNext()) {
        const QString found = it.next().captured(0);
        QVERIFY2(allowed.contains(found), qPrintable(found));
    }

    const QStringList retired = {
        "#3390ec", "#e7ebf0", "#2b82d9", "#2474c4", "#e9ebee",
        "#dde1e6", "#f4f4f5", "#a5a9ae", "#c4c9cf", "#a9afb6",
        "#707579", "#1a1a1a", "#e6e8eb",
    };
    for (const QString& token : retired) {
        QVERIFY2(!css.contains(token), qPrintable(token));
    }
}

// the Coinbase elevation is a single soft drop (0 4px 12px rgba(0,0,0,0.04));
// QSS cannot express box-shadow, so the action bar carries the effect instead.
void TestClientCore::messageActionBarHasSingleSoftShadow() {
    MessageActionBar bar;
    auto* effect = qobject_cast<QGraphicsDropShadowEffect*>(bar.graphicsEffect());
    QVERIFY(effect != nullptr);
    QCOMPARE(effect->blurRadius(), 12.0);
    QCOMPARE(effect->offset(), QPointF(0, 4));
    QCOMPARE(effect->color(), QColor(0, 0, 0, 10));
}

// The shell picks a frame per context (16px tray, 24px taskbar @150%, 256px
// Explorer); a single-frame .ico is what made the old icon look blurry.
void TestClientCore::brandAppIconExposesSizes() {
    QIcon icon(QStringLiteral(":/app.ico"));
    QList<QSize> sizes = icon.availableSizes();
    std::sort(sizes.begin(), sizes.end(),
              [](const QSize& a, const QSize& b) { return a.width() < b.width(); });
    QList<QSize> expected;
    const int wanted[] = {16, 24, 32, 48, 64, 128, 256};
    for (int side : wanted) {
        expected.append(QSize(side, side));
    }
    QCOMPARE(sizes, expected);
}

// Room badge (R2): accent disc + white speech bubble, no letter. Replaces the
// old green "1" circle in the sidebar and chat header.
void TestClientCore::roomAvatarIsBrandBadge() {
    const QImage img = chatbox::client::roomAvatar(36);
    QCOMPARE(img.size(), QSize(36, 36));
    QCOMPARE(img.pixelColor(18, 18), QColor(Qt::white));  // bubble body centre
    QCOMPARE(img.pixelColor(4, 18), QColor(chatbox::client::theme::kAccent));
    QCOMPARE(img.pixelColor(0, 0).alpha(), 0);  // round badge: corners empty
}

// The tray must show the brand mark, not Qt's stock information icon. The
// wiring itself needs a live system tray, so this is a source guard.
void TestClientCore::trayIconUsesBrandIcon() {
    QString source_path = QCoreApplication::applicationDirPath() +
                          QStringLiteral("/../../ui/main_window.cpp");
    if (!QFile::exists(source_path)) {
        source_path = QStringLiteral("../ui/main_window.cpp");
    }
    QFile source_file(source_path);
    QVERIFY2(source_file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(source_path + ": " + source_file.errorString()));
    const QString source = QString::fromUtf8(source_file.readAll());
    QVERIFY2(!source.contains(QStringLiteral("SP_MessageBoxInformation")),
             "tray must not fall back to the stock Qt information icon");
    QVERIFY2(source.contains(QStringLiteral(":/app.ico")),
             "tray must use the brand icon resource");
}

// The online member list used to always draw the letter fallback, so members
// with an uploaded photo stayed anonymous. It must resolve icons through the
// avatar cache, request missing avatars through the same "ask once with
// version 0" path used for other peers, and repaint when an avatar arrives.
// The list needs a live connection, so this is a source guard.
void TestClientCore::memberListShowsAvatarFromCache() {
    QString source_path = QCoreApplication::applicationDirPath() +
                          QStringLiteral("/../../ui/main_window.cpp");
    if (!QFile::exists(source_path)) {
        source_path = QStringLiteral("../ui/main_window.cpp");
    }
    QFile source_file(source_path);
    QVERIFY2(source_file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(source_path + ": " + source_file.errorString()));
    const QString source = QString::fromUtf8(source_file.readAll());
    QVERIFY2(source.contains(QStringLiteral("QIcon MainWindow::memberAvatarIcon(")),
             "member list must resolve icons through the cache-aware helper");
    QVERIFY2(source.contains(QStringLiteral("item->setIcon(memberAvatarIcon(")),
             "member list items must use the cache-aware avatar helper");
    QVERIFY2(!source.contains(
                 QStringLiteral("item->setIcon(avatarIcon(nickname, member_id))")),
             "member list must not hard-code the letter fallback");
    QVERIFY2(source.contains(QStringLiteral("requestPeerAvatar(member_id)")),
             "member list must request missing member avatars");
    QVERIFY2(source.contains(QStringLiteral("refreshMemberListAvatar(")),
             "arriving avatars must repaint the member list");
}

// Every fallback letter avatar must come from one palette. main_window.cpp
// used to carry its own 6-colour table (giving 483 -> #9B59B6 purple) while
// core/avatar_utils.cpp owns the 8-colour table the profile dialog uses
// (483 -> #E8684A), so the same user showed two different colours.
// The rendering needs widgets, so this is a source guard.
void TestClientCore::fallbackAvatarPaletteIsSingleSource() {
    QString source_path = QCoreApplication::applicationDirPath() +
                          QStringLiteral("/../../ui/main_window.cpp");
    if (!QFile::exists(source_path)) {
        source_path = QStringLiteral("../ui/main_window.cpp");
    }
    QFile source_file(source_path);
    QVERIFY2(source_file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(source_path + ": " + source_file.errorString()));
    const QString source = QString::fromUtf8(source_file.readAll());
    QVERIFY2(!source.contains(QStringLiteral("#9B59B6")) &&
                 !source.contains(QStringLiteral("#4CAF50")) &&
                 !source.contains(QStringLiteral("#F5A623")),
             "main_window must not carry its own fallback palette");
    QVERIFY2(source.contains(QStringLiteral("chatbox::client::fallbackAvatar(")),
             "fallback avatars must come from core/avatar_utils");
}

// The product is called ChatBox: window title, tray tooltip, unread-count
// title and the login card must all use it, and the old "ChatBox Client"
// string must not come back. Rendering needs widgets, so this is a source
// guard; brandIdentifiersAreUnified covers the internal identifiers.
void TestClientCore::appTitleUsesBrandName() {
    const auto read_source = [](const QString& relative) -> QString {
        QString path = QCoreApplication::applicationDirPath() +
                       QStringLiteral("/../../") + relative;
        if (!QFile::exists(path)) {
            path = QStringLiteral("../") + relative;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    };
    const QString window = read_source(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(!window.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY2(!window.contains(QStringLiteral("ChatBox Client")),
             "the old product name must not come back");
    QVERIFY2(window.contains(QStringLiteral("setWindowTitle(\"ChatBox\"")),
             "window title must use the ChatBox brand");
    QVERIFY2(window.contains(QStringLiteral("setToolTip(\"ChatBox\"")),
             "tray tooltip must use the ChatBox brand");
    QVERIFY2(window.contains(QStringLiteral("ChatBox (%1)")),
             "unread-count title must use the ChatBox brand");

    const QString login = read_source(QStringLiteral("ui/login_page.cpp"));
    QVERIFY2(!login.isEmpty(), "ui/login_page.cpp must be readable");
    QVERIFY2(login.contains(QStringLiteral("QLabel(\"ChatBox\"")),
             "login card title must use the ChatBox brand");
}

// Sessions and preferences used to live under the Chatroom/ChatroomClient
// identifiers. The ChatBox rename switches the store to ChatBox/ChatBox, so
// the first start has to copy the legacy values over; otherwise every user
// would be logged out by the rebrand.
void TestClientCore::settingsMigrationCopiesLegacyStore() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString legacy_path = dir.filePath(QStringLiteral("legacy.ini"));
    const QString current_path = dir.filePath(QStringLiteral("current.ini"));
    {
        QSettings legacy(legacy_path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("session/token"), QStringLiteral("token-123"));
        legacy.setValue(QStringLiteral("server/host"), QStringLiteral("192.168.2.113"));
        legacy.sync();
    }
    {
        QSettings current(current_path, QSettings::IniFormat);
        QSettings legacy(legacy_path, QSettings::IniFormat);
        QVERIFY(chatbox::client::migrateLegacySettings(current, legacy));
        QCOMPARE(current.value(QStringLiteral("session/token")).toString(),
                 QStringLiteral("token-123"));
        QCOMPARE(current.value(QStringLiteral("server/host")).toString(),
                 QStringLiteral("192.168.2.113"));
    }
    // A fresh install has nothing to copy and must not report a migration.
    {
        QSettings current(dir.filePath(QStringLiteral("fresh.ini")), QSettings::IniFormat);
        QSettings legacy(dir.filePath(QStringLiteral("empty-legacy.ini")), QSettings::IniFormat);
        QVERIFY(!chatbox::client::migrateLegacySettings(current, legacy));
    }
}

// The migration only ever fills an empty store: values written after the
// rename (for example a token refreshed by the new build) must win.
void TestClientCore::settingsMigrationNeverOverwritesCurrent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString legacy_path = dir.filePath(QStringLiteral("legacy.ini"));
    const QString current_path = dir.filePath(QStringLiteral("current.ini"));
    {
        QSettings legacy(legacy_path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("session/token"), QStringLiteral("stale-token"));
        legacy.sync();
    }
    {
        QSettings current(current_path, QSettings::IniFormat);
        current.setValue(QStringLiteral("session/token"), QStringLiteral("fresh-token"));
        current.sync();
    }
    {
        QSettings current(current_path, QSettings::IniFormat);
        QSettings legacy(legacy_path, QSettings::IniFormat);
        QVERIFY(!chatbox::client::migrateLegacySettings(current, legacy));
        QCOMPARE(current.value(QStringLiteral("session/token")).toString(),
                 QStringLiteral("fresh-token"));
    }
}

// The rename unified the internal identifiers too: executable (chatbox.exe),
// project file, QSettings store and the server-side database/log defaults all
// read chatbox/ChatBox now. This is a source guard, the values live in build
// and deployment files that the test binary cannot execute.
void TestClientCore::brandIdentifiersAreUnified() {
    const auto read_source = [](const QString& relative) -> QString {
        QString path = QCoreApplication::applicationDirPath() +
                       QStringLiteral("/../../") + relative;
        if (!QFile::exists(path)) {
            path = QStringLiteral("../") + relative;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    };
    const QString main_source = read_source(QStringLiteral("main.cpp"));
    QVERIFY2(!main_source.isEmpty(), "main.cpp must be readable");
    QVERIFY2(main_source.contains(QStringLiteral("setOrganizationName(\"ChatBox\")")),
             "QSettings organisation must be ChatBox");
    QVERIFY2(main_source.contains(QStringLiteral("setApplicationName(\"ChatBox\")")),
             "QSettings application must be ChatBox");
    QVERIFY2(main_source.contains(QStringLiteral("migrateLegacySettings(")),
             "startup must migrate the pre-ChatBox settings store");
    QVERIFY2(main_source.contains(QStringLiteral("QStringLiteral(\"Chatroom\")")) &&
                 main_source.contains(QStringLiteral("QStringLiteral(\"ChatroomClient\")")),
             "the migration must read the legacy Chatroom/ChatroomClient store");

    const QString project = read_source(QStringLiteral("chatbox.pro"));
    QVERIFY2(!project.isEmpty(), "chatbox.pro must exist next to main.cpp (project file renamed)");
    QVERIFY2(project.contains(QStringLiteral("TARGET = chatbox")),
             "the client executable must be chatbox.exe");

    const QString server_config = read_source(QStringLiteral("../server/config.h"));
    QVERIFY2(!server_config.isEmpty(), "../server/config.h must be readable");
    QVERIFY2(server_config.contains(QStringLiteral("db_name = \"chatbox\"")),
             "the server database default must be chatbox");
    QVERIFY2(server_config.contains(QStringLiteral("logs/chatbox.log")),
             "the server log default must be chatbox.log");
}

// The Inno Setup template must reference the executable the build actually
// produces (chatbox.exe). ISCC is not installed on this machine, so the
// template is never executed during the build; this source guard is what
// catches a stale name (dead shortcuts, broken post-install launch).
void TestClientCore::installerTemplateTargetsChatboxExe() {
    QString path = QCoreApplication::applicationDirPath() +
                   QStringLiteral("/../../installer/chatbox.iss");
    if (!QFile::exists(path)) {
        path = QStringLiteral("../installer/chatbox.iss");
    }
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(path + ": " + file.errorString()));
    const QString source = QString::fromUtf8(file.readAll());
    QVERIFY2(source.contains(QStringLiteral("#define MyAppExeName \"chatbox.exe\"")),
             "the installer must target the executable the build produces");
    QVERIFY2(!source.contains(QStringLiteral("_client.exe")),
             "the installer must not reference a never-built executable name");
}

// 私聊不提供 @ 功能：输入区的 @ 按钮只在房间出现，私聊消息不再触发“有人@你”。
void TestClientCore::privateChatHasNoMentionEntry() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    const QString private_section = functionSection(
        source, QStringLiteral("void MainWindow::selectPrivate("));
    QVERIFY2(private_section.contains(QStringLiteral("mention_button_->hide()")),
             "private chats must hide the mention button");
    const QString room_section = functionSection(
        source, QStringLiteral("void MainWindow::selectRoom("));
    QVERIFY2(room_section.contains(QStringLiteral("mention_button_->show()")),
             "rooms keep the mention button");
}

void TestClientCore::privatePushSkipsMentionNotification() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    const QString private_push = caseSection(
        source, QStringLiteral("case chatbox::kCmdPrivatePush:"));
    QVERIFY2(!private_push.isEmpty(), "kCmdPrivatePush handler must exist");
    QVERIFY2(!private_push.contains(QStringLiteral("showMentionNotification")),
             "private messages must not raise a mention notification");
    const QString room_push =
        caseSection(source, QStringLiteral("case chatbox::kCmdChatPush:"));
    QVERIFY2(room_push.contains(QStringLiteral("showMentionNotification")),
             "rooms keep mention notifications");
}

// 头部只留 头像 + 标题 + ⋯；资料入口是头像，消息搜索在 ⋯ 菜单里。
void TestClientCore::headerUsesAvatarAndMoreMenu() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY2(!source.contains(QStringLiteral("context_profile_button_")),
             "the header must not keep a separate profile button");
    QVERIFY2(!source.contains(QStringLiteral("load_more_button_")),
             "load-more moved into the more menu");
    QVERIFY2(!source.contains(QStringLiteral("search_messages_button")),
             "message search moved out of the composer");
    QVERIFY2(source.contains(QStringLiteral("more_button_->setPopupMode(QToolButton::InstantPopup)")),
             "the header owns an instant-popup menu button");
    QVERIFY2(source.contains(QStringLiteral("more_button_->setIcon(chatbox::client::moreDotsIcon())")),
             "the header more control uses three filled dots");
    QVERIFY2(!source.contains(QStringLiteral("more_button_->setText")),
             "the header more control must not render thin text dots");
    QVERIFY2(source.contains(QStringLiteral("menu-indicator")),
             "the header more control must hide the menu indicator arrow");
    QVERIFY2(source.contains(QStringLiteral("消息搜索")) &&
                 source.contains(QStringLiteral("加载更早消息")),
             "the more menu carries search and load-more");
    const QString avatar_click =
        functionSection(source, QStringLiteral("bool MainWindow::eventFilter("));
    QVERIFY2(avatar_click.contains(QStringLiteral("context_avatar_")) &&
                 avatar_click.contains(QStringLiteral("ContextType::Private")) &&
                 avatar_click.contains(QStringLiteral("openProfile(current_peer_id_")),
             "only a private-chat avatar opens the peer profile");
    const QString room_section =
        functionSection(source, QStringLiteral("void MainWindow::selectRoom("));
    const QString private_section =
        functionSection(source, QStringLiteral("void MainWindow::selectPrivate("));
    const QString reset_section =
        functionSection(source, QStringLiteral("void MainWindow::resetContext("));
    QVERIFY2(room_section.contains(QStringLiteral("load_more_action_->setEnabled(false)")) &&
                 private_section.contains(
                     QStringLiteral("load_more_action_->setEnabled(false)")) &&
                 reset_section.contains(
                     QStringLiteral("load_more_action_->setEnabled(false)")),
             "every context switch must disable load-more until history says otherwise");
    const QString search_section =
        functionSection(source, QStringLiteral("void MainWindow::openMessageSearch("));
    QVERIFY2(search_section.contains(QStringLiteral("ContextType::None")),
             "search must refuse to open without a selected conversation");
}

// 系统弹窗是独立开关：关掉后仍保留任务栏闪烁，但不再调用托盘气泡。
void TestClientCore::systemPopupNotificationIsOptional() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY2(source.contains(QStringLiteral("settings/system_popup")),
             "the system-popup preference must be persisted");
    const QString private_notify = functionSection(
        source, QStringLiteral("void MainWindow::showPrivateNotification("));
    QVERIFY2(private_notify.contains(QStringLiteral("system_popup_enabled_")),
             "private popups must honour the system-popup switch");
    const QString mention_notify = functionSection(
        source, QStringLiteral("void MainWindow::showMentionNotification("));
    QVERIFY2(mention_notify.contains(QStringLiteral("system_popup_enabled_")),
             "mention popups must honour the system-popup switch");
    QVERIFY2(source.contains(
                 QStringLiteral("notifications_enabled_ && system_popup_enabled_ && tray_ != nullptr")),
             "friend-request popups must honour both switches");
    const QString dialog = readClientSource(QStringLiteral("ui/settings_dialog.cpp"));
    QVERIFY2(dialog.contains(QStringLiteral("系统弹窗通知")),
             "the settings dialog exposes the system-popup switch");
}

// 路径可能含空格，命令行必须整体加引号；--minimized 让开机启动静默驻留托盘。
void TestClientCore::autostartCommandCarriesMinimizedFlag() {
    const QString exe = QStringLiteral("C:/Program Files/ChatBox/chatbox.exe");
    const QString expected =
        QStringLiteral("\"%1\" --minimized").arg(QDir::toNativeSeparators(exe));
    QCOMPARE(chatbox::client::autostartCommand(exe), expected);
}

void TestClientCore::autostartSettingWritesAndRemovesRunValue() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings run_key(dir.filePath(QStringLiteral("run.ini")), QSettings::IniFormat);
    const QString exe = QStringLiteral("C:/Apps/ChatBox/chatbox.exe");
    chatbox::client::applyAutostartSetting(run_key, true, exe);
    QCOMPARE(run_key.value(QStringLiteral("ChatBox")).toString(),
             chatbox::client::autostartCommand(exe));
    chatbox::client::applyAutostartSetting(run_key, false, exe);
    QVERIFY(!run_key.contains(QStringLiteral("ChatBox")));
}

// 开机启动静默进托盘；没有托盘时必须正常显示窗口，不能变成不可见进程。
// 第二实例直接退出；崩溃遗留的锁允许过期。
void TestClientCore::startupHonoursMinimizedAndSingleInstance() {
    const QString source = readClientSource(QStringLiteral("main.cpp"));
    QVERIFY2(!source.isEmpty(), "main.cpp must be readable");
    QVERIFY2(source.contains(QStringLiteral("--minimized")),
             "startup must understand the autostart flag");
    QVERIFY2(source.contains(QStringLiteral("shouldStartHidden(")),
             "startup must use the testable visibility policy");
    QVERIFY2(!source.contains(QStringLiteral("QSystemTrayIcon::isSystemTrayAvailable()")),
             "Qt's Windows tray probe is a constant; the shell must be checked instead");
    QVERIFY2(source.contains(QStringLiteral("QLockFile")),
             "a second instance must not run concurrently");
    QVERIFY2(source.contains(QStringLiteral("setStaleLockTime")),
             "a crashed instance must not block startup forever");
}

// Qt's Windows platform plugin reports a system tray unconditionally, so the
// shell check is separate and the visibility decision is a pure policy.
void TestClientCore::startupPolicyRequiresUsableTray() {
    QVERIFY(chatbox::client::shouldStartHidden(true, true));
    QVERIFY(!chatbox::client::shouldStartHidden(true, false));
    QVERIFY(!chatbox::client::shouldStartHidden(false, true));
    QVERIFY(!chatbox::client::shouldStartHidden(false, false));
}

// Turning the alert master switch off must also hide unread badges and the
// window-title counter, not only the taskbar flash and popups.
void TestClientCore::unreadBadgesHonourAlertsSwitch() {
    QCOMPARE(chatbox::client::displayedUnread(true, 5), 5);
    QCOMPARE(chatbox::client::displayedUnread(false, 5), 0);
    QCOMPARE(chatbox::client::displayedUnread(true, -1), 0);
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    const QString refresh = functionSection(
        source, QStringLiteral("void MainWindow::refreshFriendListView("));
    QVERIFY2(refresh.contains(QStringLiteral("displayedUnread(notifications_enabled_")),
             "the conversation list must route unread counts through the policy");
}

// Opening a fresh directory must create the schema described in the design
// spec, and repeating the call for the same account must be idempotent.
void TestClientCore::historyStoreCreatesSchema() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY2(store.open(dir.path(), 1101, &error), qPrintable(error));
    QVERIFY(store.isOpen());
    QCOMPARE(store.userId(), quint64(1101));
    QCOMPARE(store.directory(), QDir(dir.path()).absolutePath());
    QVERIFY(store.sizeBytes() > 0);

    {
        QSqlDatabase db = QSqlDatabase::database(QStringLiteral("history_1101"), false);
        QVERIFY(db.isOpen());
        QVERIFY(db.tables().contains(QStringLiteral("messages")));
        QVERIFY(db.tables().contains(QStringLiteral("conversations")));
        QSqlQuery version(db);
        QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")) && version.next());
        QCOMPARE(version.value(0).toInt(), 1);
        QSqlQuery indexes(db);
        QVERIFY(indexes.exec(QStringLiteral("PRAGMA index_list(messages)")));
        QStringList index_names;
        while (indexes.next()) index_names << indexes.value(1).toString();
        QVERIFY(index_names.contains(QStringLiteral("uk_msg_server")));
        QVERIFY(index_names.contains(QStringLiteral("idx_msg_page")));
    }

    QVERIFY(store.open(dir.path(), 1101, &error));  // 幂等
    store.close();
    QVERIFY(!store.isOpen());
}

// A path that is a regular file can never host the database, so opening must
// fail with a Chinese message instead of silently succeeding.
void TestClientCore::historyStoreReportsOpenFailure() {
    QTemporaryDir dir;
    QFile blocker(dir.filePath(QStringLiteral("not-a-dir")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY(!store.open(blocker.fileName(), 1102, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.isOpen());
}

// Two live stores must not share one connection name: the second open has to
// fail instead of stealing the first store's connection.
void TestClientCore::historyStoreRejectsSecondInstance() {
    QTemporaryDir dir;
    QString error;
    chatbox::client::HistoryStore first;
    QVERIFY(first.open(dir.path(), 1103, &error));
    chatbox::client::HistoryStore second;
    QVERIFY(!second.open(dir.path(), 1103, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(first.isOpen());
    QCOMPARE(first.directory(), QDir(dir.path()).absolutePath());
    first.close();
    QVERIFY2(second.open(dir.path(), 1103, &error), qPrintable(error));
    second.close();
}

// Confirmed messages must round-trip through the local store: the newest
// page comes back in ascending order and the older page is reachable with
// the pagination cursor.
void TestClientCore::historyStoreSaveAndLoadRecent() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1201, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "one", "2026-10-20 10:00:00")));
    QVERIFY(store.saveMessage(confirmedMessage(2, 7, "two", "2026-10-20 10:01:00")));
    QVERIFY(store.saveMessage(confirmedMessage(3, 7, "three", "2026-10-20 10:02:00")));
    const auto last = store.recent(chatbox::client::ChatContext::kRoom, 7, 2);
    QCOMPARE(last.size(), 2);
    QCOMPARE(last.at(0).message_id, quint64(2));
    QCOMPARE(last.at(1).message_id, quint64(3));
    const auto older = store.before(chatbox::client::ChatContext::kRoom, 7, 2, 5);
    QCOMPARE(older.size(), 1);
    QCOMPARE(older.at(0).message_id, quint64(1));
    QVERIFY(store.before(chatbox::client::ChatContext::kRoom, 7, 1, 5).isEmpty());
}

// The same server message must never be stored twice; a repeated write
// updates the existing row in place.
void TestClientCore::historyStoreDedupesByMessageId() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1202, &error));
    QVERIFY(store.saveMessage(confirmedMessage(5, 7, "hello")));
    QVERIFY(store.saveMessage(confirmedMessage(5, 7, "edited")));
    QCOMPARE(store.messageCount(), qint64(1));
    QCOMPARE(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).at(0).content,
             QString("edited"));
}

// Unconfirmed (still pending) messages stay in memory only: they must not
// reach the local database.
void TestClientCore::historyStoreSkipsUnconfirmedMessages() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1203, &error));
    auto pending = confirmedMessage(0, 7, "not confirmed");
    pending.status = chatbox::client::MessageStatus::kSending;
    QVERIFY(!store.saveMessage(pending));
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(!store.hasMessages());
}

// A recall wipes the body from the local copy as well.
void TestClientCore::historyStoreMarksRecalled() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1204, &error));
    QVERIFY(store.saveMessage(confirmedMessage(7, 7, "to be recalled")));
    QVERIFY(store.markRecalled(chatbox::client::ChatContext::kRoom, 7, 7));
    const auto rows = store.recent(chatbox::client::ChatContext::kRoom, 7, 10);
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows.at(0).recalled);
    QVERIFY(rows.at(0).content.isEmpty());
    QVERIFY(!store.markRecalled(chatbox::client::ChatContext::kRoom, 7, 999));

    // 撤回后会话摘要必须跟着清空：侧栏不能再显示已撤回的正文。
    const auto summaries = store.conversations();
    QCOMPARE(summaries.size(), 1);
    QVERIFY(summaries.at(0).last_content.isEmpty());
}

// 撤回必须按会话定位：房间与私聊各自独立编号，
// 私聊里撤回 message_id=7 不能把房间里同号的消息一起清空。
void TestClientCore::historyStoreRecallIsConversationScoped() {
    QTemporaryDir dir;
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY(store.open(dir.path(), 1208, &error));
    QVERIFY(store.saveMessage(confirmedMessage(7, 7, "room text")));
    chatbox::client::ChatMessage private_message = confirmedMessage(7, 8, "private text");
    private_message.context = chatbox::client::ChatContext::kPrivate;
    QVERIFY(store.saveMessage(private_message));
    QVERIFY(store.markRecalled(chatbox::client::ChatContext::kPrivate, 8, 7));
    const auto room = store.recent(chatbox::client::ChatContext::kRoom, 7, 10);
    const auto priv = store.recent(chatbox::client::ChatContext::kPrivate, 8, 10);
    QCOMPARE(room.size(), 1);
    QCOMPARE(priv.size(), 1);
    QVERIFY(!room.at(0).recalled);
    QCOMPARE(room.at(0).content, QStringLiteral("room text"));
    QVERIFY(priv.at(0).recalled);
    QVERIFY(priv.at(0).content.isEmpty());
}

// Two accounts share one database file but must never see each other's rows.
void TestClientCore::historyStoreIsolatesUsers() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1205, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "mine")));
    QVERIFY(store.open(dir.path(), 1206, &error));
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).isEmpty());
    QVERIFY(store.open(dir.path(), 1205, &error));
    QCOMPARE(store.messageCount(), qint64(1));
}

// A room and a private chat are separate conversations even when they use
// the same numeric target id.
void TestClientCore::historyStoreIsolatesConversations() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1207, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "room message")));
    auto priv = confirmedMessage(2, 8, "private message");
    priv.context = chatbox::client::ChatContext::kPrivate;
    QVERIFY(store.saveMessage(priv));
    QCOMPARE(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).size(), 1);
    QCOMPARE(store.recent(chatbox::client::ChatContext::kPrivate, 8, 10).size(), 1);
    QVERIFY(store.recent(chatbox::client::ChatContext::kPrivate, 7, 10).isEmpty());
}

// Local search is scoped to the current conversation and must treat the
// keyword literally: % and _ are not wildcards.
void TestClientCore::historyStoreSearchFindsInConversation() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1208, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "hello 100% done")));
    QVERIFY(store.saveMessage(confirmedMessage(2, 7, "hello world")));
    auto priv = confirmedMessage(3, 8, "hello private");
    priv.context = chatbox::client::ChatContext::kPrivate;
    QVERIFY(store.saveMessage(priv));

    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "hello", 50).size(), 2);
    const auto priv_hits = store.search(chatbox::client::ChatContext::kPrivate, 8, "hello", 50);
    QCOMPARE(priv_hits.size(), 1);
    QCOMPARE(priv_hits.at(0).message_id, quint64(3));
    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "100%", 50).size(), 1);
    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "%", 50).size(), 1);  // 只中 100%
    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "_", 50).size(), 0);  // 下划线被转义
    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "hello", 1).size(), 1);
}

// 会话摘要：saveConversation 只保证会话行存在并写标题，last_* 由消息写入推进；
// 更旧的一页历史不得把 last_* 往回改。
void TestClientCore::historyStoreConversationSummaries() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1301, &error));
    chatbox::client::ConversationRow row;
    row.context = chatbox::client::ChatContext::kRoom;
    row.target_id = 7;
    row.title = QStringLiteral("大厅");
    QVERIFY(store.saveConversation(row));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "one", "2026-10-20 10:00:00")));
    QVERIFY(store.saveMessage(confirmedMessage(2, 7, "two", "2026-10-20 10:01:00")));
    // 更旧的消息不得把 last_* 往回改
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "one edited", "2026-10-20 09:00:00")));
    auto rows = store.conversations();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).title, QStringLiteral("大厅"));
    QCOMPARE(rows.at(0).last_message_id, quint64(2));
    QCOMPARE(rows.at(0).last_content, QString("two"));
}

// 清理：clearOlderThan 只删当前账号的过期消息并重算会话摘要（空会话删除），
// clearAll 清空当前账号的全部本地记录。
void TestClientCore::historyStoreClearAllAndOlderThan() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1302, &error));
    const QString old_time = QDateTime::currentDateTimeUtc().addDays(-40)
                                 .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "old room", old_time)));
    QVERIFY(store.saveMessage(confirmedMessage(2, 7, "new room",
                                              QDateTime::currentDateTimeUtc()
                                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))));
    auto priv = confirmedMessage(3, 8, "old private", old_time);
    priv.context = chatbox::client::ChatContext::kPrivate;
    QVERIFY(store.saveMessage(priv));

    QVERIFY(store.clearOlderThan(30));
    auto rows = store.conversations();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).context, chatbox::client::ChatContext::kRoom);
    QCOMPARE(rows.at(0).last_message_id, quint64(2));

    QVERIFY(store.clearAll());
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(store.conversations().isEmpty());
}

// 迁移目录：migrate=true 把现有记录复制到新目录，migrate=false 在新目录建空库。
void TestClientCore::historyStorePathMoveCopiesData() {
    QTemporaryDir first; QTemporaryDir second; QTemporaryDir third;
    chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(first.path(), 1303, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "keep me")));
    QCOMPARE(store.messageCount(), qint64(1));

    QVERIFY2(store.moveTo(second.path(), true, &error), qPrintable(error));
    QVERIFY(store.isOpen());
    QCOMPARE(store.directory(), QDir(second.path()).absolutePath());
    QCOMPARE(store.messageCount(), qint64(1));
    QVERIFY(QFile::exists(QDir(first.path()).filePath(QStringLiteral("history.sqlite"))));

    QVERIFY2(store.moveTo(third.path(), false, &error), qPrintable(error));
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).isEmpty());
}

// 目标目录已有本地库时必须拒绝迁移，并保持当前连接在旧目录上打开。
void TestClientCore::historyStoreRejectsExistingTarget() {
    QTemporaryDir source; QTemporaryDir target;
    QString error;
    {
        chatbox::client::HistoryStore other;
        QVERIFY(other.open(target.path(), 1304, &error));
    }
    chatbox::client::HistoryStore store;
    QVERIFY(store.open(source.path(), 1305, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "keep")));
    QVERIFY(!store.moveTo(target.path(), true, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.isOpen());
    QCOMPARE(store.directory(), QDir(source.path()).absolutePath());
    QCOMPARE(store.messageCount(), qint64(1));

    QFile blocker(source.filePath(QStringLiteral("blocker")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QVERIFY(!chatbox::client::HistoryStore::ensureWritableDirectory(
        blocker.fileName() + QStringLiteral("/sub"), &error));
    QVERIFY(chatbox::client::HistoryStore::ensureWritableDirectory(
        source.filePath(QStringLiteral("writable")), &error));
}

// The portable package must ship the Qt SQL module and the SQLite driver
// plugin, otherwise the local history store cannot open its database on a
// machine without a Qt installation.
void TestClientCore::buildScriptShipsSqlDriver() {
    const QString build = readClientSource(QStringLiteral("build_client.ps1"));
    QVERIFY2(!build.isEmpty(), "build_client.ps1 must be readable");
    QVERIFY(build.contains(QStringLiteral("Qt5Sql.dll")));
    QVERIFY(build.contains(QStringLiteral("sqldrivers")));
    QVERIFY(build.contains(QStringLiteral("qsqlite.dll")));

    const QString package = readClientSource(QStringLiteral("package_client.ps1"));
    QVERIFY2(!package.isEmpty(), "package_client.ps1 must be readable");
    QVERIFY(package.contains(QStringLiteral("\"imageformats\"")));
    QVERIFY(package.contains(QStringLiteral("\"sqldrivers\"")));
}

// 打开会话时先用本地历史首屏渲染，再由在线历史覆盖：
// 两个选择入口都必须先 loadInitialHistory()，
// 预渲染本身只读本地库并把结果交给 replaceHistory()。
void TestClientCore::conversationOpensFromLocalHistoryFirst() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    const QString room = functionSection(source, QStringLiteral("void MainWindow::selectRoom("));
    const QString priv = functionSection(source, QStringLiteral("void MainWindow::selectPrivate("));
    QVERIFY(room.contains(QStringLiteral("loadInitialHistory(")));
    QVERIFY(priv.contains(QStringLiteral("loadInitialHistory(")));
    const QString load = functionSection(source, QStringLiteral("void MainWindow::loadInitialHistory("));
    QVERIFY(load.contains(QStringLiteral("history_store_.recent(")));
    QVERIFY(load.contains(QStringLiteral("replaceHistory(")));
}

// 后台会话收到的推送同样要落本地库（不论是不是当前会话），
// 私聊推送还要补一条会话摘要，便于离线侧栏显示。
void TestClientCore::incomingPushesArePersistedForOtherConversations() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    const QString room_push = caseSection(source, QStringLiteral("case chatbox::kCmdChatPush:"));
    const QString private_push = caseSection(source, QStringLiteral("case chatbox::kCmdPrivatePush:"));
    QVERIFY(room_push.contains(QStringLiteral("history_store_.saveMessage(")));
    QVERIFY(private_push.contains(QStringLiteral("history_store_.saveMessage(")));
    QVERIFY(private_push.contains(QStringLiteral("saveConversation(")));
}

// 后台会话的撤回推送同样要落到本地历史库：
// 否则本地副本里还留着用户已经撤回的正文。
void TestClientCore::recallPushUpdatesBackgroundHistory() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    const QString recall = caseSection(source, QStringLiteral("case chatbox::kCmdMessageRecall:"));
    QVERIFY(!recall.isEmpty());
    QVERIFY(recall.contains(QStringLiteral("history_store_.markRecalled(")));
    QVERIFY(recall.contains(QStringLiteral("ChatContext::kRoom")));
    QVERIFY(recall.contains(QStringLiteral("ChatContext::kPrivate")));
}

// 登录页不再保留「查看本地记录」行：无网络时由主窗口自动进入只读离线模式。
void TestClientCore::loginPageHasNoLocalHistoryEntry() {
    LoginPage page;
    QVERIFY(page.findChild<QPushButton*>(QStringLiteral("LocalHistoryButton")) == nullptr);
}

// 服务器/端口/TLS 不是每次登录都要填：默认收在登录卡片的横向三点入口里，
// 新安装默认走公网，展开后才显示可编辑字段。
void TestClientCore::loginPageHidesServerFieldsBehindMoreDots() {
    QSettings settings;
    settings.remove(QStringLiteral("server"));
    settings.sync();

    LoginPage page;
    auto* toggle = page.findChild<QToolButton*>(QStringLiteral("AdvancedOptionsButton"));
    auto* panel = page.findChild<QWidget*>(QStringLiteral("AdvancedOptionsPanel"));
    QVERIFY(toggle != nullptr);
    QVERIFY(panel != nullptr);
    QVERIFY(toggle->text().isEmpty());
    QVERIFY(!toggle->icon().isNull());
    QVERIFY(toggle->iconSize().width() >= 24);
    QVERIFY(panel->isHidden());

    auto* host = page.findChild<QLineEdit*>(QStringLiteral("ServerHostEdit"));
    auto* port = page.findChild<QSpinBox*>(QStringLiteral("ServerPortSpin"));
    auto* tls = page.findChild<QCheckBox*>(QStringLiteral("ServerTlsCheck"));
    QVERIFY(host != nullptr);
    QVERIFY(port != nullptr);
    QVERIFY(tls != nullptr);
    QCOMPARE(host->text(), QStringLiteral("frp-oil.com"));
    QCOMPARE(port->value(), 15318);
    QVERIFY(tls->isChecked());

    toggle->click();
    QVERIFY(!panel->isHidden());
    toggle->click();
    QVERIFY(panel->isHidden());
}

// 昵称只在注册时出现：登录态点“注册”进入注册态，
// 主按钮变“注册”，次按钮变“返回登录”；返回后昵称隐藏。
void TestClientCore::loginPageShowsNicknameOnlyInRegisterMode() {
    LoginPage page;
    auto* nickname = page.findChild<QLineEdit*>(QStringLiteral("NicknameEdit"));
    auto* login = page.findChild<QPushButton*>(QStringLiteral("PrimaryButton"));
    auto* register_button = page.findChild<QPushButton*>(QStringLiteral("RegisterButton"));
    QVERIFY(nickname != nullptr);
    QVERIFY(login != nullptr);
    QVERIFY(register_button != nullptr);
    QVERIFY(nickname->isHidden());
    QCOMPARE(login->text(), QStringLiteral("登录"));
    QCOMPARE(register_button->text(), QStringLiteral("注册"));

    register_button->click();
    QVERIFY(!nickname->isHidden());
    QCOMPARE(login->text(), QStringLiteral("注册"));
    QCOMPARE(register_button->text(), QStringLiteral("返回登录"));

    register_button->click();
    QVERIFY(nickname->isHidden());
    QCOMPARE(login->text(), QStringLiteral("登录"));
    QCOMPARE(register_button->text(), QStringLiteral("注册"));
}

void TestClientCore::loginPageHasAutoLoginAndRememberPasswordOptions() {
    QSettings settings;
    settings.remove(QStringLiteral("login"));
    settings.sync();

    LoginPage page;
    auto* auto_login = page.findChild<QCheckBox*>(QStringLiteral("AutoLoginCheck"));
    auto* remember = page.findChild<QCheckBox*>(QStringLiteral("RememberPasswordCheck"));
    QVERIFY(auto_login != nullptr);
    QVERIFY(remember != nullptr);
    QVERIFY(auto_login->isChecked());
    QVERIFY(!remember->isChecked());
}

void TestClientCore::loginPageReloadsRememberedPasswordAfterLogout() {
    QSettings settings;
    settings.remove(QStringLiteral("login"));
    settings.sync();
    settings.setValue(QStringLiteral("login/remember_password"), true);
    settings.sync();
    QVERIFY(chatbox::client::saveSavedCredentials(QStringLiteral("alice"),
                                                   QStringLiteral("Secret123")));

    LoginPage page;
    auto* username = page.findChild<QLineEdit*>(QStringLiteral("UsernameEdit"));
    auto* password = page.findChild<QLineEdit*>(QStringLiteral("PasswordEdit"));
    QVERIFY(username != nullptr);
    QVERIFY(password != nullptr);
    QCOMPARE(username->text(), QStringLiteral("alice"));
    QCOMPARE(password->text(), QStringLiteral("Secret123"));

    page.clearPassword();
    QVERIFY(password->text().isEmpty());
    page.resetToLogin();
    QCOMPARE(username->text(), QStringLiteral("alice"));
    QCOMPARE(password->text(), QStringLiteral("Secret123"));

    chatbox::client::clearSavedCredentials();
    settings.remove(QStringLiteral("login"));
    settings.sync();
}

void TestClientCore::credentialStoreEncryptsSavedPassword() {
    QSettings settings;
    settings.remove(QStringLiteral("login"));
    settings.sync();
    if (!chatbox::client::passwordStorageSupported()) {
        QSKIP("secure password storage is only available on Windows");
    }

    QVERIFY(chatbox::client::saveSavedCredentials(QStringLiteral("alice"),
                                                   QStringLiteral("Secret123")));
    QSettings stored;
    const QString encoded = stored.value(QStringLiteral("login/password")).toString();
    QVERIFY(!encoded.isEmpty());
    QVERIFY(!encoded.contains(QStringLiteral("Secret123")));

    QString username;
    QString password;
    QVERIFY(chatbox::client::loadSavedCredentials(&username, &password));
    QCOMPARE(username, QStringLiteral("alice"));
    QCOMPARE(password, QStringLiteral("Secret123"));

    chatbox::client::clearSavedCredentials();
    QVERIFY(!chatbox::client::loadSavedCredentials(&username, &password));
}

void TestClientCore::settingsDialogDefaultsToPublicServer() {
    QSettings settings;
    settings.remove(QStringLiteral("server"));
    settings.sync();

    SettingsDialog dialog(QStringLiteral("U00001001"), QString(), 0, false);
    auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("ServerHostEdit"));
    auto* port = dialog.findChild<QSpinBox*>(QStringLiteral("ServerPortSpin"));
    auto* tls = dialog.findChild<QCheckBox*>(QStringLiteral("ServerTlsCheck"));
    QVERIFY(host != nullptr);
    QVERIFY(port != nullptr);
    QVERIFY(tls != nullptr);
    QCOMPARE(host->text(), QStringLiteral("frp-oil.com"));
    QCOMPARE(port->value(), 15318);
    QVERIFY(tls->isChecked());
}

// 离线模式必须是只读的本地视图：所有写操作都要被挡住，
// 搜索与翻页改走本地历史库；断网时自动进入，不再保留手动入口。
void TestClientCore::offlineModeIsReadOnlyAndLocal() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY(source.contains(QStringLiteral("enterOfflineMode(")));
    QVERIFY(source.contains(QStringLiteral("enterOfflineMode(true)")));
    QVERIFY(source.contains(QStringLiteral("离线模式 · 仅显示本地记录")));
    QVERIFY(!source.contains(QStringLiteral("LocalHistoryButton")));
    QVERIFY(!source.contains(QStringLiteral("openLocalHistory")));
    const QString recall = functionSection(source, QStringLiteral("void MainWindow::sendRecall("));
    QVERIFY(recall.contains(QStringLiteral("!online_")));
    const QString search = functionSection(source,
        QStringLiteral("void MainWindow::requestMessageSearch("));
    QVERIFY(search.contains(QStringLiteral("offline_mode_")));
    QVERIFY(search.contains(QStringLiteral("history_store_.search(")));
    const QString room = functionSection(source, QStringLiteral("void MainWindow::selectRoom("));
    QVERIFY(room.contains(QStringLiteral("offline_mode_")));
    QVERIFY(room.contains(QStringLiteral("loadInitialHistory(")));
    const QString leave = functionSection(source, QStringLiteral("void MainWindow::leaveOfflineMode("));
    QVERIFY(leave.contains(QStringLiteral("offline_mode_ = false")));
    const QString save = functionSection(source, QStringLiteral("void MainWindow::saveSession("));
    QVERIFY(save.contains(QStringLiteral("history/last_user_id")));
    const QString clear = functionSection(source, QStringLiteral("void MainWindow::clearSession("));
    QVERIFY(!clear.contains(QStringLiteral("history/last_user_id")));
}

// 设置页要能显示本地聊天记录的存放位置、占用大小，并提供三个操作入口。
// 库不可用（缺少 SQLite 驱动）时按钮必须禁用，避免用户点了才发现没库。
void TestClientCore::settingsDialogShowsHistoryInfo() {
    QTemporaryDir dir;
    SettingsDialog dialog(QStringLiteral("U00001001"), dir.path(), 4096, true);
    auto* path = dialog.findChild<QLabel*>(QStringLiteral("HistoryPathLabel"));
    auto* size = dialog.findChild<QLabel*>(QStringLiteral("HistorySizeLabel"));
    QVERIFY(path != nullptr);
    QVERIFY(size != nullptr);
    QCOMPARE(QDir(path->text()), QDir(dir.path()));
    QCOMPARE(size->text(), QStringLiteral("4.0 KB"));
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("ChangeHistoryDirButton")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("OpenHistoryDirButton")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("ClearHistoryButton")) != nullptr);
    dialog.setHistoryInfo(dir.path(), 3 * 1024 * 1024, false);
    QCOMPARE(size->text(), QStringLiteral("3.0 MB"));
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("ClearHistoryButton"))->isEnabled());
}

// 设置页的三个历史操作都要有中文文案，并落到 MainWindow 的落盘逻辑上。
void TestClientCore::settingsDialogExposesHistoryActions() {
    const QString dialog = readClientSource(QStringLiteral("ui/settings_dialog.cpp"));
    QVERIFY(!dialog.isEmpty());
    QVERIFY(dialog.contains(QStringLiteral("更改目录")));
    QVERIFY(dialog.contains(QStringLiteral("打开目录")));
    QVERIFY(dialog.contains(QStringLiteral("清理")));
    QVERIFY(dialog.contains(QStringLiteral("historyPathChangeRequested")));
    QVERIFY(dialog.contains(QStringLiteral("historyClearAllRequested")));
    QVERIFY(dialog.contains(QStringLiteral("historyClearOlderRequested")));
    QVERIFY(dialog.contains(QStringLiteral("不影响服务器历史")));
    const QString window = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY(window.contains(QStringLiteral("settings/history/path")));
    QVERIFY(window.contains(QStringLiteral("moveTo(")));
}

QTEST_MAIN(TestClientCore)
#include "test_client_core.moc"