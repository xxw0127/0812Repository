# ChatBox 客户端交互调整（批次 A）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 私聊去掉 @、会话头部改为「头像进资料 + ⋮ 菜单」、设置增加系统弹窗开关与开机自启动。

**Architecture:** 全部改动在 Qt Widgets 客户端 `chatbox/client` 内：`MainWindow` 调整头部/输入区控件与通知分支，`SettingsDialog` 扩展信号与控件，新增 `core/autostart` 封装 Windows 注册表 Run 项，`main.cpp` 增加 `--minimized` 与单实例锁。服务端与协议零改动。

**Tech Stack:** Qt 5.15.2 / C++17 / MinGW 8.1 / qmake；QtTest 做单元与源码守卫测试。

**Spec:** `docs/superpowers/specs/2026-10-20-client-ux-and-local-history-design.md`（需求 1/2/3，§10 默认值）

## Global Constraints

- 客户端 Qt 5.15 + C++17 + MinGW 8.1（qmake），不使用 C++20 特性，不新增第三方库。
- 只改客户端；服务端、协议、数据库零改动。
- 己方气泡绿色 `#E6F7F0` 保留；不引入深色模式；不使用 Coinbase 商标/字体。
- 头文件保护用 `#ifndef/#define/#endif`；4 空格缩进；大括号 K&R；源文件 UTF-8。
- 部署/脚本文件保持 LF；不提交 `build/`、`release/`、`dist/` 产物。
- 测试入口：`chatbox/client/tests/run_client_tests.ps1`；构建入口：`chatbox/client/build_client.ps1`。
- 重新构建 `chatbox.exe` 前必须先停掉正在运行的 `chatbox.exe`（Windows 会锁定 exe），构建完再启动。
- 所有文案中文、UTF-8；提交信息中文，格式 `feat: ...` / `fix: ...`。

## Review Focus

1. 房间 ↔ 私聊切换后，`@` 按钮、头像手型光标、⋮ 菜单状态必须立刻跟随当前会话，不能残留上一会话的状态。
2. 未选择任何会话时打开 ⋮ 菜单：搜索项不得崩溃，`加载更早消息` 必须禁用。
3. 切换会话后 `has_more_` 必须重置，菜单里的「加载更早消息」不能出现可点但无数据的假启用。
4. 房间会话点击头像不得打开任何人的资料（只有私聊且 `peer_id != 0` 才打开）。
5. 关闭「系统弹窗通知」后，私聊、被@、好友申请三类都不能再弹系统通知；关闭「消息提醒」后连任务栏闪烁也停止。
6. 开机自启动命令必须给 exe 路径加引号（路径含空格）并带 `--minimized`；取消后注册表值必须消失。
7. `--minimized` 启动且系统托盘不可用时（资源管理器未启动/远程会话）不能把应用藏成不可见进程，必须回退为正常显示。
8. 第二个实例必须直接退出，不能写坏任何数据；上一个实例崩溃后锁必须能过期（stale lock），不能永久阻塞启动。

---

### Task 1: 私聊去掉 @

**Files:**
- Modify: `chatbox/client/ui/main_window.h:188-193`（成员区）
- Modify: `chatbox/client/ui/main_window.cpp:446-462`（输入区）、`:518`（连接）、`:2638-2681`（selectRoom）、`:2709-2752`（selectPrivate）、`:3177-3179`（私聊推送）、`:3985-3989`（resetContext）
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: 现有 `MainWindow::insertMention()`、`showMentionNotification()`。
- Produces: 成员 `QPushButton* mention_button_`；`MainWindow::selectPrivate` 隐藏它、`selectRoom` 显示它；私聊推送不再调用 `showMentionNotification`。

- [ ] **Step 1: 在测试文件顶部添加源码读取辅助函数**

在 `test_client_core.cpp` 的匿名命名空间（没有就新建，放在 include 之后、`class TestClientCore` 之前）加入：

```cpp
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

}  // namespace
```

- [ ] **Step 2: 声明并编写失败测试**

在 `private slots:` 末尾（`installerTemplateTargetsChatboxExe();` 之后）加入：

```cpp
    void privateChatHasNoMentionEntry();
    void privatePushSkipsMentionNotification();
```

在文件末尾 `QTEST_MAIN` 之前加入：

```cpp
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
```

- [ ] **Step 3: 运行测试确认失败**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: FAIL —— `privateChatHasNoMentionEntry` 找不到 `mention_button_->hide()`。

- [ ] **Step 4: 修改头文件成员**

在 `main_window.h` 的输入区成员旁加入：

```cpp
    QPushButton* mention_button_ = nullptr;
```

- [ ] **Step 5: 输入区改用成员并在切换会话时控制可见性**

`buildChatPage()` 中把局部 `auto* mention_button = new QPushButton("@", composer);` 改为：

```cpp
    mention_button_ = new QPushButton("@", composer);
    mention_button_->setObjectName("IconButton");
    mention_button_->setFixedSize(38, 38);
    mention_button_->setToolTip("提及成员");
```

`input_layout->addWidget(mention_button);` → `input_layout->addWidget(mention_button_);`
`connect(mention_button, ...)` → `connect(mention_button_, &QPushButton::clicked, this, &MainWindow::insertMention);`

`selectRoom()` 在设置 `context_type_` 之后加：

```cpp
    mention_button_->show();
```

`selectPrivate()` 同样位置加：

```cpp
    mention_button_->hide();
```

`resetContext()` 在清空输入框之后加：

```cpp
    if (mention_button_ != nullptr) {
        mention_button_->hide();
    }
```

- [ ] **Step 6: 删除私聊的提及通知分支**

`kCmdPrivatePush` 中删除：

```cpp
            if (!nickname_.isEmpty() && content.contains("@" + nickname_)) {
                showMentionNotification(nickname, content);
            }
```

房间（`kCmdChatPush`）里的同一段保留不动。

- [ ] **Step 7: 运行测试确认通过**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: PASS，`Totals: <N> passed, 0 failed`。

- [ ] **Step 8: 提交**

```powershell
cd D:\OpenCode_WorkSpace
$env:Path = "D:\git\cmd;" + $env:Path
git add chatbox/client/ui/main_window.h chatbox/client/ui/main_window.cpp chatbox/client/tests/test_client_core.cpp
git commit -m "feat: 私聊隐藏 @ 入口且不再触发提及提醒"
```

---

### Task 2: 头像进资料 + 右上角 ⋮ 菜单

**Files:**
- Modify: `chatbox/client/ui/main_window.h`（成员、`eventFilter` 声明）
- Modify: `chatbox/client/ui/main_window.cpp`（头部、输入区、连接、事件过滤、load_more 调用点）
- Modify: `chatbox/client/resources/style.qss`（`QToolButton#MoreButton`）
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: `MainWindow::openProfile(quint64, const QString&)`、`openMessageSearch()`、`onLoadMoreClicked()`、`has_more_`。
- Produces: `QToolButton* more_button_`、`QMenu* more_menu_`、`QAction* load_more_action_`；`bool MainWindow::eventFilter(QObject*, QEvent*) override`；删除 `context_profile_button_`、`load_more_button_`、`search_messages_button`。

- [ ] **Step 1: 声明并编写失败测试**

`private slots:` 末尾加入：

```cpp
    void headerUsesAvatarAndMoreMenu();
```

`QTEST_MAIN` 之前加入：

```cpp
// 头部只留 头像 + 标题 + ⋮；资料入口是头像，消息搜索在 ⋮ 菜单里。
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
    QVERIFY2(source.contains(QStringLiteral("消息搜索")) &&
                 source.contains(QStringLiteral("加载更早消息")),
             "the more menu carries search and load-more");
    const QString avatar_click =
        functionSection(source, QStringLiteral("bool MainWindow::eventFilter("));
    QVERIFY2(avatar_click.contains(QStringLiteral("context_avatar_")) &&
                 avatar_click.contains(QStringLiteral("ContextType::Private")) &&
                 avatar_click.contains(QStringLiteral("openProfile(current_peer_id_")),
             "only a private-chat avatar opens the peer profile");
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: FAIL —— 仍存在 `context_profile_button_`。

- [ ] **Step 3: 头文件声明**

`main_window.h` 前置声明区加入 `class QAction; class QEvent; class QMenu; class QToolButton;`；`protected:` 增加：

```cpp
    bool eventFilter(QObject* watched, QEvent* event) override;
```

成员区把

```cpp
    QPushButton* context_profile_button_ = nullptr;
```

替换为

```cpp
    QToolButton* more_button_ = nullptr;
    QMenu* more_menu_ = nullptr;
    QAction* load_more_action_ = nullptr;
```

并把 `QPushButton* load_more_button_ = nullptr;` 删除。

- [ ] **Step 4: 头部控件与菜单**

`buildChatPage()` 头部：`context_avatar_` 创建后加：

```cpp
    context_avatar_->setCursor(Qt::ArrowCursor);
    context_avatar_->setToolTip("查看资料");
    context_avatar_->installEventFilter(this);
```

删除 `context_profile_button_` 的创建、`header_row->addWidget`、`connect` 三段；`header_row->addWidget(context_label_, 1);` 之后加：

```cpp
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
```

删除输入区的 `search_messages_button` 创建、`input_layout->addWidget`、`connect` 三段（`mention_button_` 与发送按钮保留）。

`selectRoom()`、`selectPrivate()`、`resetContext()` 中所有 `load_more_button_` 操作替换为：

```cpp
    load_more_action_->setEnabled(false);
```

（`resetContext()` 去掉 `if (load_more_button_ != nullptr)` 外壳。）

`requestHistory()` 里的 `load_more_button_->setEnabled(false);` 改为 `load_more_action_->setEnabled(false);`。
`kCmdHistoryResp` 里

```cpp
            load_more_button_->setEnabled(true);
            load_more_button_->setVisible(has_more_);
```

改为

```cpp
            load_more_action_->setEnabled(has_more_);
```

`selectPrivate()` 中设置头像后加：

```cpp
    context_avatar_->setCursor(Qt::PointingHandCursor);
```

`selectRoom()` 与 `resetContext()` 中设置头像后加：

```cpp
    context_avatar_->setCursor(Qt::ArrowCursor);
```

- [ ] **Step 5: 实现 eventFilter**

在 `closeEvent` 之前加入（并 `#include <QMouseEvent>`）：

```cpp
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
```

- [ ] **Step 6: ⋮ 按钮样式**

`resources/style.qss` 末尾追加（保持 LF）：

```css
QToolButton#MoreButton {
    background: transparent;
    border: none;
    border-radius: 6px;
    color: #0A0B0D;
    font-size: 18px;
    padding: 2px;
}

QToolButton#MoreButton:hover {
    background: #EEF0F3;
}

QToolButton#MoreButton:pressed {
    background: #E6E8EC;
}
```

- [ ] **Step 7: 运行测试确认通过**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: PASS。

- [ ] **Step 8: 提交**

```powershell
cd D:\OpenCode_WorkSpace
$env:Path = "D:\git\cmd;" + $env:Path
git add chatbox/client/ui/main_window.h chatbox/client/ui/main_window.cpp chatbox/client/resources/style.qss chatbox/client/tests/test_client_core.cpp
git commit -m "feat: 会话头部改为头像进资料 + 右上角更多菜单"
```

---

### Task 3: 设置拆分「消息提醒」与「系统弹窗通知」

**Files:**
- Modify: `chatbox/client/ui/settings_dialog.h`
- Modify: `chatbox/client/ui/settings_dialog.cpp`
- Modify: `chatbox/client/ui/main_window.h`（`system_popup_enabled_` 成员）
- Modify: `chatbox/client/ui/main_window.cpp`（构造初始化、通知函数、好友申请弹窗、openSettings）
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: `notifications_enabled_`（总开关）、`tray_->showMessage()`、`QApplication::alert()`。
- Produces: `settingsSaved(host, port, tls, notifications, system_popup)` 五参数信号；`bool system_popup_enabled_`；QSettings 键 `settings/system_popup`（默认 true）。

- [ ] **Step 1: 声明并编写失败测试**

`private slots:` 末尾加入：

```cpp
    void systemPopupNotificationIsOptional();
```

`QTEST_MAIN` 之前加入：

```cpp
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
```

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: FAIL —— 没有 `settings/system_popup`。

- [ ] **Step 3: SettingsDialog 增加开关并扩展信号**

`settings_dialog.h`：

```cpp
    void settingsSaved(const QString& host, quint16 port, bool tls, bool notifications,
                       bool system_popup);
```

成员增加 `QCheckBox* system_popup_check_ = nullptr;`。

`settings_dialog.cpp`：

```cpp
    notifications_check_ = new QCheckBox("消息提醒（任务栏闪烁/未读角标）", this);
    notifications_check_->setChecked(
        settings.value("settings/notifications", true).toBool());
    system_popup_check_ = new QCheckBox("系统弹窗通知", this);
    system_popup_check_->setChecked(
        settings.value("settings/system_popup", true).toBool());

    form->addRow("通知", notifications_check_);
    form->addRow("", system_popup_check_);
```

`saveSettings()` 发射：

```cpp
    emit settingsSaved(host, port, tls, notifications_check_->isChecked(),
                       system_popup_check_->isChecked());
```

`resize(440, 330)` → `resize(440, 370)`。

- [ ] **Step 4: 主窗口保存并生效**

`main_window.h` 增加 `bool system_popup_enabled_ = true;`。

构造函数初始化：

```cpp
    system_popup_enabled_ =
        initial_settings.value("settings/system_popup", true).toBool();
```

`openSettings()` 的 lambda 参数改为 `(const QString& host, quint16 port, bool tls, bool notifications, bool system_popup)`，写入：

```cpp
                settings.setValue("settings/system_popup", system_popup);
```

并赋值 `system_popup_enabled_ = system_popup;`，日志追加 ` system_popup=%d` 与 `system_popup ? 1 : 0`。

`showPrivateNotification()`：

```cpp
void MainWindow::showPrivateNotification(const QString& nickname,
                                         const QString& content) {
    if (!notifications_enabled_) {
        return;
    }
    QApplication::alert(this, 3000);
    if (system_popup_enabled_ && tray_ != nullptr) {
        tray_->showMessage("私聊消息", nickname + ": " + content,
                           QSystemTrayIcon::Information, 3000);
    }
}
```

`showMentionNotification()` 同样把 `tray_ != nullptr` 改成 `system_popup_enabled_ && tray_ != nullptr`。

两处好友申请通知的 `if (notifications_enabled_ && tray_ != nullptr)` 改为：

```cpp
    if (notifications_enabled_ && system_popup_enabled_ && tray_ != nullptr) {
```

- [ ] **Step 5: 运行测试确认通过**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: PASS。

- [ ] **Step 6: 提交**

```powershell
cd D:\OpenCode_WorkSpace
$env:Path = "D:\git\cmd;" + $env:Path
git add chatbox/client/ui/settings_dialog.h chatbox/client/ui/settings_dialog.cpp chatbox/client/ui/main_window.h chatbox/client/ui/main_window.cpp chatbox/client/tests/test_client_core.cpp
git commit -m "feat: 设置拆分消息提醒与系统弹窗开关"
```

---

### Task 4: 开机自启动 + 托盘菜单 + 单实例 + `--minimized`

**Files:**
- Create: `chatbox/client/core/autostart.h`
- Create: `chatbox/client/core/autostart.cpp`
- Modify: `chatbox/client/chatbox.pro`、`chatbox/client/tests/tests.pro`
- Modify: `chatbox/client/ui/settings_dialog.h` / `.cpp`
- Modify: `chatbox/client/ui/main_window.h` / `.cpp`
- Modify: `chatbox/client/main.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: `QSettings`（NativeFormat）、`QSystemTrayIcon`、`QCoreApplication::applicationFilePath()`。
- Produces:
  - `QString chatbox::client::autostartCommand(const QString& exe_path)` → `"<exe>" --minimized`
  - `void chatbox::client::applyAutostartSetting(QSettings& run_key, bool enabled, const QString& exe_path)`
  - `bool chatbox::client::setAutostartEnabled(bool enabled, const QString& exe_path)`
  - `bool chatbox::client::isAutostartEnabled()`
  - `settingsSaved(host, port, tls, notifications, system_popup, autostart)` 六参数。

- [ ] **Step 1: 声明并编写失败测试**

`private slots:` 末尾加入：

```cpp
    void autostartCommandCarriesMinimizedFlag();
    void autostartSettingWritesAndRemovesRunValue();
    void startupHonoursMinimizedAndSingleInstance();
```

文件顶部 include：

```cpp
#include <QDir>

#include "core/autostart.h"
```

`QTEST_MAIN` 之前加入：

```cpp
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
    QVERIFY2(source.contains(QStringLiteral("QSystemTrayIcon::isSystemTrayAvailable()")),
             "minimized startup must fall back to a visible window without a tray");
    QVERIFY2(source.contains(QStringLiteral("QLockFile")),
             "a second instance must not run concurrently");
    QVERIFY2(source.contains(QStringLiteral("setStaleLockTime")),
             "a crashed instance must not block startup forever");
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: 编译失败 —— `core/autostart.h` 不存在。

- [ ] **Step 3: 实现 autostart**

`core/autostart.h`：

```cpp
#ifndef CHATBOX_CLIENT_CORE_AUTOSTART_H
#define CHATBOX_CLIENT_CORE_AUTOSTART_H

#include <QString>

class QSettings;

namespace chatbox {
namespace client {

// Command line stored in HKCU Run: "<exe>" --minimized. The quotes keep paths
// with spaces intact; --minimized makes the app start in the tray.
QString autostartCommand(const QString& exe_path);

// Writes/removes the ChatBox value in the supplied store. Separated from the
// registry lookup so tests can drive it with an INI file.
void applyAutostartSetting(QSettings& run_key, bool enabled, const QString& exe_path);

// Applies the setting to HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
// Returns false when the platform or the store cannot honour it.
bool setAutostartEnabled(bool enabled, const QString& exe_path);
bool isAutostartEnabled();

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_AUTOSTART_H
```

`core/autostart.cpp`：

```cpp
#include "core/autostart.h"

#include <QDir>
#include <QSettings>

namespace chatbox {
namespace client {

namespace {

#ifdef Q_OS_WIN
const char* const kRunKeyPath =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif
const char* const kRunValueName = "ChatBox";

}  // namespace

QString autostartCommand(const QString& exe_path) {
    return QStringLiteral("\"%1\" --minimized")
        .arg(QDir::toNativeSeparators(exe_path));
}

void applyAutostartSetting(QSettings& run_key, bool enabled,
                           const QString& exe_path) {
    if (enabled) {
        run_key.setValue(QString::fromLatin1(kRunValueName), autostartCommand(exe_path));
    } else {
        run_key.remove(QString::fromLatin1(kRunValueName));
    }
    run_key.sync();
}

bool setAutostartEnabled(bool enabled, const QString& exe_path) {
#ifdef Q_OS_WIN
    QSettings run_key(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    applyAutostartSetting(run_key, enabled, exe_path);
    return run_key.status() == QSettings::NoError;
#else
    Q_UNUSED(enabled);
    Q_UNUSED(exe_path);
    return false;
#endif
}

bool isAutostartEnabled() {
#ifdef Q_OS_WIN
    QSettings run_key(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    return !run_key.value(QString::fromLatin1(kRunValueName)).toString().isEmpty();
#else
    return false;
#endif
}

}  // namespace client
}  // namespace chatbox
```

`chatbox.pro` 的 SOURCES/HEADERS 加 `core/autostart.cpp` / `core/autostart.h`；`tests.pro` 同样加入。

- [ ] **Step 4: 设置对话框加「开机自启动」**

信号改为六参数：

```cpp
    void settingsSaved(const QString& host, quint16 port, bool tls, bool notifications,
                       bool system_popup, bool autostart);
```

include `core/autostart.h`，成员加 `QCheckBox* autostart_check_ = nullptr;`。

```cpp
    autostart_check_ = new QCheckBox("开机自启动（静默到托盘）", this);
    autostart_check_->setChecked(chatbox::client::isAutostartEnabled());
    form->addRow("启动", autostart_check_);
```

发射时追加 `autostart_check_->isChecked()`；`resize(440, 370)` → `resize(440, 410)`。

- [ ] **Step 5: 主窗口应用并刷新自启动**

`openSettings()` lambda 改为六参数，末尾：

```cpp
                settings.setValue("settings/autostart", autostart);
                if (autostart && !chatbox::client::setAutostartEnabled(
                                      true, QCoreApplication::applicationFilePath())) {
                    setStatus("开机自启动写入注册表失败", true);
                    qWarning("autostart enable failed");
                } else if (!autostart) {
                    chatbox::client::setAutostartEnabled(false,
                                                         QCoreApplication::applicationFilePath());
                }
```

构造函数中（`notifications_enabled_` 初始化之后）：

```cpp
    if (initial_settings.value("settings/autostart", false).toBool()) {
        if (!chatbox::client::setAutostartEnabled(
                true, QCoreApplication::applicationFilePath())) {
            qWarning("autostart refresh failed");
        }
    }
```

include `core/autostart.h`。

- [ ] **Step 6: 托盘菜单与最小化召回**

`buildChatPage()` 创建托盘后加：

```cpp
        auto* tray_menu = new QMenu(this);
        tray_menu->addAction("打开 ChatBox", this, [this] {
            showNormal();
            raise();
            activateWindow();
        });
        tray_menu->addAction("退出", this, [this] { close(); });
        tray_->setContextMenu(tray_menu);
        connect(tray_, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger ||
                        reason == QSystemTrayIcon::DoubleClick) {
                        showNormal();
                        raise();
                        activateWindow();
                    }
                });
```

（保留原有 `setIcon/setToolTip/show`。）

- [ ] **Step 7: main.cpp 单实例与 `--minimized`**

include：

```cpp
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>
```

在 `QApplication` 创建、组织/应用名设置之后加入：

```cpp
    const QString lock_directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(lock_directory);
    QLockFile instance_lock(QDir(lock_directory).filePath(QStringLiteral("chatbox.lock")));
    instance_lock.setStaleLockTime(30000);
    if (!instance_lock.tryLock(200)) {
        QMessageBox::information(nullptr, "ChatBox", "ChatBox 已在运行。");
        return 0;
    }
```

窗口显示处改为：

```cpp
    const bool start_minimized =
        QCoreApplication::arguments().contains(QStringLiteral("--minimized"));
    MainWindow window;
    if (!start_minimized || !QSystemTrayIcon::isSystemTrayAvailable()) {
        window.show();
    }
```

- [ ] **Step 8: 运行测试确认通过**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: PASS。

- [ ] **Step 9: 提交**

```powershell
cd D:\OpenCode_WorkSpace
$env:Path = "D:\git\cmd;" + $env:Path
git add chatbox/client/core/autostart.h chatbox/client/core/autostart.cpp chatbox/client/chatbox.pro chatbox/client/tests/tests.pro chatbox/client/ui/settings_dialog.h chatbox/client/ui/settings_dialog.cpp chatbox/client/ui/main_window.h chatbox/client/ui/main_window.cpp chatbox/client/main.cpp chatbox/client/tests/test_client_core.cpp
git commit -m "feat: 开机自启动、托盘菜单与单实例启动"
```

---

### Task 5: 文档、整包构建与手动验收

**Files:**
- Modify: `chatbox/client/README.md`
- Verify: `chatbox/client/build_client.ps1`

**Interfaces:**
- Consumes: 前四个任务的成果。
- Produces: 更新后的功能说明与真实测试数字；可运行的 `build/release/chatbox.exe`。

- [ ] **Step 1: 更新客户端 README**

在功能列表补充：私聊无 @；头部头像进资料、⋮ 菜单含消息搜索与加载更早消息；设置含「消息提醒」「系统弹窗通知」「开机自启动（静默到托盘）」。更新测试数字为本次 `Totals:` 的实际值。

- [ ] **Step 2: 跑完整测试**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: `Totals: <N> passed, 0 failed`。

- [ ] **Step 3: 构建客户端**

先停掉运行中的实例（Windows 锁定 exe）：

```powershell
Get-Process chatbox -ErrorAction SilentlyContinue | Stop-Process -Force
powershell -ExecutionPolicy Bypass -File chatbox/client/build_client.ps1
```

Expected: `Build complete: ...\build\release\chatbox.exe`。

- [ ] **Step 4: 手动验收**

- 私聊：输入区没有 `@`；收到含自己昵称的消息只出现普通私聊通知。
- 房间：`@` 按钮还在，提及提醒正常。
- 头部：房间/私聊都只有 `头像 + 标题 + ⋮`；点私聊头像打开资料；房间头像无动作且是普通箭头光标。
- ⋮：未选会话时「加载更早消息」灰；进入房间后翻页按钮可用且能加载更早消息。
- 设置：勾选「开机自启动」后运行 `reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v ChatBox` 能看到带引号和 `--minimized` 的命令；取消后查询报“找不到”。
- 通知：关闭「系统弹窗通知」后私聊/被@/好友申请不再弹系统通知，但任务栏闪烁仍在。
- 最小化：`chatbox.exe --minimized` 启动后只出现在托盘，托盘菜单「打开 ChatBox」能召回，「退出」能结束进程。
- 单实例：保持一个实例运行，再次启动 `chatbox.exe` 应提示“已在运行”并退出。

- [ ] **Step 5: 提交**

```powershell
cd D:\OpenCode_WorkSpace
$env:Path = "D:\git\cmd;" + $env:Path
git add chatbox/client/README.md
git commit -m "docs: 更新客户端交互调整说明与测试数字"
```
