# Qt 客户端 MVP → 可用版（第一阶段）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 让 Qt 客户端具备消息发送状态、失败保留与重发、聊天式渲染、本地时间、连接横幅和登录错误提示，从演示 MVP 变成可日常使用的纯文本聊天客户端。

**Architecture:** 新增 `MessageStore` / `Outbox` / `MessageRenderer` 三个核心单元；`MainWindow` 只负责协议收发和 UI 编排。保留现有 `QTextBrowser` 富文本渲染，不重写为 `QListView`。发送响应和错误按请求 `seq` 关联到具体 `client_msg_id`；服务端时间统一按 UTC 解析，显示时转本地时区。

**Tech Stack:** Qt 5.15.2 Widgets、C++17、MinGW 8.1、原生 Winsock、OpenSSL 4.0.3、QtTest

**Spec:** `docs/superpowers/specs/2026-10-18-client-usability-audit.md`

**Status:** 已完成（2026-10-18）。

## Global Constraints

- 只改客户端；不修改协议、服务端或数据库。
- 服务端 `created_at` 是 UTC，客户端只在显示层转本地时间。
- 发送重试复用原 `client_msg_id`；服务端已有幂等去重。
- Outbox 只保存在进程内；应用退出后未发送消息丢失，这是本阶段已知限制。
- 保留 `QTextBrowser`，历史分页仍为每页 50 条。
- UI 只能在主线程操作；网络仍由 `NetworkThread` 独立线程负责。
- 不新增第三方依赖；测试使用 QtTest。
- 核心状态机（时间、MessageStore、Outbox、Renderer）用 QtTest 覆盖；`MainWindow` 的协议/UI 粘合层用计划中的手工验收覆盖。
- 工作区不是 Git 仓库；每个任务以“客户端测试通过 + 应用构建通过”作为检查点，不使用 commit 步骤。
- 构建命令统一使用：
  ```powershell
  cd D:\OpenCode_WorkSpace\chatroom\client
  .\build_client.ps1
  ```

## Review Focus

- 断网或发送瞬间断线：消息必须显示失败，不能出现“本地显示已发送但服务端没收到”。
- 超时后迟到的成功响应，或重发后的重复响应：同一条消息不能出现两个气泡，也不能把失败状态错改成其他消息。
- 加载历史时存在发送中/失败消息：历史替换不能丢掉 Outbox 中的消息。
- 服务端 UTC 时间跨本地日期边界：时间显示和日期分隔必须使用本地时区。
- 消息内容包含 `<script>`、`<table>`、引号或换行：只能作为文本显示，不能破坏聊天布局。

---

## File Structure

- `client/core/chat_message.h`：消息上下文、状态、数据结构和 UTC 时间字段。
- `client/core/message_time.h/.cpp`：解析服务端 UTC、格式化本地时间/日期。
- `client/core/message_store.h/.cpp`：当前会话消息、历史合并、状态更新。
- `client/core/outbox.h/.cpp`：进程内待发/失败消息、超时、重发状态。
- `client/core/message_renderer.h/.cpp`：消息列表 → 安全 HTML。
- `client/ui/main_window.h/.cpp`：协议收发、seq 关联、连接状态、UI 编排。
- `client/tests/tests.pro`、`client/tests/test_client_core.cpp`、`client/tests/run_client_tests.ps1`：QtTest 测试工程。
- `client/chatroom_client.pro`：加入新 core 文件。
- `client/README.md`：构建/测试/已知限制。

---

### Task 1: 核心类型、UTC 时间与测试工程

**Files:**
- Create: `client/core/chat_message.h`
- Create: `client/core/message_time.h`
- Create: `client/core/message_time.cpp`
- Create: `client/tests/tests.pro`
- Create: `client/tests/test_client_core.cpp`
- Create: `client/tests/run_client_tests.ps1`

**Interfaces:**
- Produces:
  ```cpp
  namespace chatroom::client {
  enum class ChatContext { kRoom, kPrivate };
  enum class MessageStatus { kSending, kSent, kFailed };
  struct ChatMessage {
      QString client_msg_id;
      quint64 message_id = 0;
      ChatContext context = ChatContext::kRoom;
      quint64 target_id = 0;
      quint64 sender_id = 0;
      QString sender_name;
      QString content;
      QDateTime created_at_utc;
      bool outgoing = false;
      MessageStatus status = MessageStatus::kSent;
  };
  QDateTime parseServerUtc(const QString& text);
  QString formatLocalTime(const QDateTime& utc);
  QString formatLocalDate(const QDateTime& utc);
  }
  ```
- `parseServerUtc` 接受 `yyyy-MM-dd HH:mm:ss`，返回 `Qt::UTC` 时间；格式错误返回无效 `QDateTime`。
- `formatLocalTime` 返回 `HH:mm`，无效时间返回 `--:--`。
- `formatLocalDate` 返回 `yyyy-MM-dd`，无效时间返回 `--`。

- [x] **Step 1: 写失败测试**

在 `client/tests/test_client_core.cpp` 写：

```cpp
#include <QtTest>

#include "core/message_time.h"

class TestClientCore : public QObject {
    Q_OBJECT

private slots:
    void parseServerUtc();
    void formatLocalTimeAndDate();
    void invalidTimestamp();
};

void TestClientCore::parseServerUtc() {
    const QDateTime parsed = chatroom::client::parseServerUtc("2026-10-18 12:00:00");
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.timeSpec(), Qt::UTC);
    QCOMPARE(parsed.date(), QDate(2026, 10, 18));
    QCOMPARE(parsed.time(), QTime(12, 0, 0));
}

void TestClientCore::formatLocalTimeAndDate() {
    const QDateTime local(QDate(2026, 10, 18), QTime(10, 30, 0), Qt::LocalTime);
    const QDateTime utc = local.toUTC();
    QCOMPARE(chatroom::client::formatLocalTime(utc), QString("10:30"));
    QCOMPARE(chatroom::client::formatLocalDate(utc), QString("2026-10-18"));
}

void TestClientCore::invalidTimestamp() {
    const QDateTime parsed = chatroom::client::parseServerUtc("not-a-time");
    QVERIFY(!parsed.isValid());
    QCOMPARE(chatroom::client::formatLocalTime(parsed), QString("--:--"));
    QCOMPARE(chatroom::client::formatLocalDate(parsed), QString("--"));
}

QTEST_APPLESS_MAIN(TestClientCore)
#include "test_client_core.moc"
```

`client/tests/tests.pro`：

```qmake
QT += core testlib
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = client_tests

INCLUDEPATH += $$PWD/..
SOURCES += \
    test_client_core.cpp \
    ../core/message_time.cpp
HEADERS += \
    ../core/chat_message.h \
    ../core/message_time.h
```

`client/tests/run_client_tests.ps1`：

```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$qt = "D:\Qt\5.15.2\mingw81_64"
$mingw = "D:\Qt\Tools\mingw810_64"
$env:PATH = "$qt\bin;$mingw\bin;$env:PATH"
Push-Location $root
try {
    & "$qt\bin\qmake.exe" tests.pro
    if ($LASTEXITCODE -ne 0) { throw "qmake failed" }
    & "$mingw\bin\mingw32-make.exe" -f Makefile.Release -j8
    if ($LASTEXITCODE -ne 0) { throw "make failed" }
    & ".\release\client_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "client tests failed" }
} finally {
    Pop-Location
}
```

- [x] **Step 2: 运行测试，确认失败**

Run:
```powershell
cd D:\OpenCode_WorkSpace\chatroom\client\tests
.\run_client_tests.ps1
```
Expected: 编译失败，提示找不到 `core/message_time.h` 或 `message_time.cpp`。

- [x] **Step 3: 实现核心类型和时间函数**

`client/core/chat_message.h` 使用 `#ifndef` 保护，包含 `<QDateTime>`、`<QString>`、`<QtGlobal>`、`<cstdint>`，定义上面的枚举和结构体。

`client/core/message_time.cpp`：
- `parseServerUtc` 用 `QDateTime::fromString(text, "yyyy-MM-dd HH:mm:ss")`，成功后 `setTimeSpec(Qt::UTC)`。
- `formatLocalTime` 对无效时间返回 `"--:--"`，否则 `utc.toLocalTime().toString("HH:mm")`。
- `formatLocalDate` 对无效时间返回 `"--"`，否则 `utc.toLocalTime().toString("yyyy-MM-dd")`。

- [x] **Step 4: 运行测试，确认通过**

Run: `.\run_client_tests.ps1`
Expected: `Totals: 3 passed, 0 failed`。

---

### Task 2: 消息存储与 Outbox

**Files:**
- Create: `client/core/message_store.h`
- Create: `client/core/message_store.cpp`
- Create: `client/core/outbox.h`
- Create: `client/core/outbox.cpp`
- Modify: `client/tests/tests.pro`
- Modify: `client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class MessageStore {
  public:
      void reset(ChatContext context, quint64 target_id);
      void replaceHistory(const QVector<ChatMessage>& history);
      void prependHistory(const QVector<ChatMessage>& history);
      void appendIncoming(const ChatMessage& message);
      void addPending(const ChatMessage& message);
      bool markSending(const QString& client_msg_id);
      bool markSent(const QString& client_msg_id, quint64 message_id,
                    const QDateTime& created_at_utc);
      bool markFailed(const QString& client_msg_id);
      int markAllSendingFailed();
      bool isCurrent(ChatContext context, quint64 target_id) const;
      const QVector<ChatMessage>& messages() const;
  };

  class Outbox {
  public:
      void add(const ChatMessage& message, const QDateTime& now);
      bool markSending(const QString& client_msg_id, const QDateTime& now);
      bool markSent(const QString& client_msg_id);
      bool markFailed(const QString& client_msg_id);
      int markAllSendingFailed();
      QVector<QString> expireSending(const QDateTime& now, int timeout_seconds);
      bool contains(const QString& client_msg_id) const;
      QVector<ChatMessage> pending() const;
      void clear();
  };
  ```

行为约束：

- `MessageStore::replaceHistory` 用历史覆盖已发送消息，但保留：
  - 所有非 `kSent` 的当前消息；
  - `message_id` 不在历史中的当前消息（例如刚收到 ack、历史页还没包含）。
  保留消息追加在历史之后，按原顺序。
- `MessageStore::prependHistory` 按 `message_id` 去重，把旧页插到前面。
- `MessageStore::addPending` 不重复添加相同 `client_msg_id`；状态置 `kSending`。
- `MessageStore::markSending` 把指定 `client_msg_id` 的状态置回 `kSending`。
- `MessageStore::markSent` 按 `client_msg_id` 更新 `message_id`、`created_at_utc`、`kSent`。
- `Outbox::markSent` 从 Outbox 移除；`markFailed` 保留但置失败。
- `Outbox::expireSending` 把超过 `timeout_seconds` 仍为 `kSending` 的消息置 `kFailed`，返回这些 `client_msg_id`。

- [x] **Step 1: 写失败测试**

在 `test_client_core.cpp` 增加 include：
```cpp
#include "core/message_store.h"
#include "core/outbox.h"
```

增加以下测试槽和实现：

```cpp
void messageStorePendingAndAck();
void messageStoreHistoryPreservesPending();
void messageStorePrependHistory();
void outboxExpiresAndMarksFailed();
void outboxMarksSentAndAllFailed();
```

测试断言：

- `messageStorePendingAndAck`：
  - 新建 `ChatMessage`，`client_msg_id="p1"`，`status=kSending`。
  - `store.addPending` 后 `messages().size()==1`。
  - `markSent("p1", 42, utc)` 返回 true；消息 `message_id==42`、`status==kSent`。
  - 再次 `markSent("p1", 42, utc)` 仍返回 true，消息数量不变（覆盖重复响应）。
  - `markSending("p1")` 后状态回到 `kSending`。
  - 重复 `addPending` 不增加数量。
- `messageStoreHistoryPreservesPending`：
  - 先 `addPending(p1)`。
  - `replaceHistory({h1})` 后顺序为 `h1, p1`，`p1` 仍是 `kSending`。
- `messageStorePrependHistory`：
  - 当前消息 id=10，`prependHistory({id=8, id=9})` 后顺序为 8,9,10；重复 prepend 不重复。
- `outboxExpiresAndMarksFailed`：
  - `add(msg, t0)`；`expireSending(t0.addSecs(5), 10)` 返回空；`expireSending(t0.addSecs(11), 10)` 返回 `p1`，`pending()[0].status==kFailed`。
  - 再调用 `markSent("p1")` 返回 true，`contains("p1")==false`（覆盖超时后迟到成功响应）。
- `outboxMarksSentAndAllFailed`：
  - `add` 两条；`markSent(p1)` 后 `contains(p1)==false`；
  - `markAllSendingFailed()` 返回 1，剩余消息状态为 `kFailed`。

在 `tests.pro` 的 `SOURCES` 加入：
```qmake
../core/message_store.cpp \
../core/outbox.cpp
```
`HEADERS` 加入两个新头文件。

- [x] **Step 2: 运行测试，确认失败**

Run: `.\run_client_tests.ps1`
Expected: 编译失败，提示 `MessageStore` / `Outbox` 未定义。

- [x] **Step 3: 实现 `MessageStore` 和 `Outbox`**

- `MessageStore` 内部使用 `QVector<ChatMessage>`，按 `client_msg_id` 查找。
- `replaceHistory` 先收集历史 `message_id` 集合，再筛选当前消息。
- `prependHistory` 按 `message_id` 去重后插入到头部。
- `Outbox` 内部使用 `struct Entry { ChatMessage message; QDateTime last_attempt_at; int attempts; }`。
- `add` 不重复添加；`markSending` 更新尝试时间和次数；`expireSending` 只处理 `kSending`。

- [x] **Step 4: 运行测试，确认通过**

Run: `.\run_client_tests.ps1`
Expected: 全部通过（Task 1 的 3 个 + 本任务 5 个）。

---

### Task 3: 聊天 HTML 渲染器

**Files:**
- Create: `client/core/message_renderer.h`
- Create: `client/core/message_renderer.cpp`
- Modify: `client/tests/tests.pro`
- Modify: `client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  QString renderConversationHtml(const QVector<ChatMessage>& messages,
                                 quint64 self_user_id);
  ```
- 输出完整 `<html><body>...</body></html>`。
- 日期变化时插入居中日期分隔。
- 自己的消息 `align="right"`，别人的消息 `align="left"`。
- 失败消息包含 `<a href="retry:<client_msg_id>">重发</a>`；非失败消息不包含。
- 所有内容、昵称必须 `toHtmlEscaped()`；换行转 `<br>`。

- [x] **Step 1: 写失败测试**

增加测试槽：

```cpp
void rendererEscapesHtml();
void rendererShowsRetryOnlyWhenFailed();
void rendererAddsDateSeparator();
void rendererAlignsOwnMessages();
```

断言：

- content 为 `<script>alert("x")</script>` 时，输出包含 `&lt;script&gt;`，不包含原始 `<script>`。
- `status=kFailed` 时包含 `retry:p1`；`status=kSent` 时不包含。
- 两条消息的本地日期分别为 2026-10-18 和 2026-10-19 时，输出同时包含两个日期字符串。
- 自己的消息输出包含 `align="right"`；别人的包含 `align="left"`。

在 `tests.pro` 加入 `../core/message_renderer.cpp` 和头文件。

- [x] **Step 2: 运行测试，确认失败**

Run: `.\run_client_tests.ps1`
Expected: 编译失败，提示 `renderConversationHtml` 未定义。

- [x] **Step 3: 实现渲染器**

使用 `QStringList` 拼接 HTML；每条消息用 `<table width="100%">` 布局：

- 日期分隔：`<div align="center">2026-10-18</div>`。
- 自己的消息：`<table width="100%"><tr><td align="right">...`。
- 别人的消息：`<table width="100%"><tr><td align="left">...`。
- 昵称只在别人消息显示。
- 时间和状态放在消息下方：
  - `kSending`：`发送中…`
  - `kFailed`：`发送失败 <a href="retry:UUID">重发</a>`
  - `kSent`：`formatLocalTime(message.created_at_utc)`
- 内容先 `toHtmlEscaped()`，再把 `\n` 替换为 `<br>`。

- [x] **Step 4: 运行测试，确认通过**

Run: `.\run_client_tests.ps1`
Expected: 全部通过。

---

### Task 4: 历史、实时消息与渲染接入

**Files:**
- Modify: `client/chatroom_client.pro`
- Modify: `client/ui/main_window.h`
- Modify: `client/ui/main_window.cpp`

**Interfaces:**
- Consumes: `ChatMessage`、`MessageStore`、`renderConversationHtml`、`parseServerUtc`。
- Produces in `MainWindow`:
  ```cpp
  enum class ScrollMode { kBottom, kTop, kPreserve };
  void renderCurrentConversation(ScrollMode mode);
  void addIncomingMessage(const ChatMessage& message);
  bool currentChatContext(ChatContext& context, quint64& target_id) const;
  ```
- `history_lines_` 从成员变量删除，替换为 `MessageStore message_store_;`。
- 历史响应解析为 `QVector<ChatMessage>`：
  - `created_at_utc = parseServerUtc(created_at)`
  - `outgoing = sender_id == user_id_`
  - `status = MessageStatus::kSent`
- 最新页调用 `replaceHistory`；加载更早页调用 `prependHistory`。
- `appendMessage` 改为构造 `ChatMessage` 后调用 `addIncomingMessage`。

**Steps:**
- [x] 更新 `.pro`，加入 `core/chat_message.h`、`core/message_time.cpp`、`core/message_store.cpp`、`core/message_renderer.cpp`。
- [x] 在 `main_window.h` 替换 `history_lines_`，加入 `renderCurrentConversation`、`addIncomingMessage`、`currentChatContext`。
- [x] `selectRoom` / `selectPrivate` 调用 `message_store_.reset(...)`，清空视图后请求历史。
- [x] `kCmdHistoryResp` 构造消息列表，调用 `replaceHistory` / `prependHistory`，再按“最新页底部、旧页顶部”渲染。
- [x] `kCmdChatPush` 和当前私聊的 `kCmdPrivatePush` 构造 incoming 消息并 `addIncomingMessage`；不在当前会话的私聊仍走未读/提醒。
- [x] 会话列表的 `last_time` 也按 UTC 解析后转本地时间再放入 tooltip。
- [x] 将 `appendMessage` 的调用点改为消息模型；删除旧的 `[时间] 发送者: 内容` 渲染。
- [x] 编译应用：

Run:
```powershell
cd D:\OpenCode_WorkSpace\chatroom\client
.\build_client.ps1
```
Expected: 构建成功，无编译错误。

- [x] **手动验证**：登录后进入房间，历史消息有日期分隔、左右对齐；切换房间/私聊后消息不串；时间显示为本地时间。
- [x] **手动验证**：私聊收到消息后未读数、托盘提醒仍正常。

---

### Task 5: 发送状态、失败重发与超时

**Files:**
- Modify: `client/ui/main_window.h`
- Modify: `client/ui/main_window.cpp`

**Interfaces:**
- `MainWindow` 新增：
  ```cpp
  bool sendFrame(const chatroom::Frame& frame);
  void sendMessage(const ChatMessage& message);
  void retryMessage(const QString& client_msg_id);
  void onSendTimeout();
  QHash<quint32, QString> pending_sequences_;
  Outbox outbox_;
  QTimer* send_timeout_timer_ = nullptr;
  ```
- `sendFrame` 从 `void` 改为 `bool`：未连接或 `network_ == nullptr` 时返回 false。
- 发送流程：
  1. `onSendClicked` 构造 `ChatMessage`，`status=kSending`，`created_at_utc=QDateTime::currentDateTimeUtc()`，`outgoing=true`。
  2. `outbox_.add(message, now)`，`message_store_.addPending(message)`，清空输入，渲染。
  3. `sendMessage` 分配 seq，记录 `pending_sequences_[seq] = client_msg_id`，发送。
  4. `sendFrame` 返回 false 时：`outbox_.markFailed(id)`、`message_store_.markFailed(id)`、渲染、状态栏提示。
- 成功响应：
  - `kCmdChatSendResp` / `kCmdPrivateSendResp` 解析 `message_id`、`created_at`。
  - 从 `pending_sequences_` 取 seq 对应的 id。
  - `outbox_.markSent(id)`、`message_store_.markSent(id, message_id, parseServerUtc(created_at))`、渲染。
- 错误响应：
  - `handleError` 增加 `quint32 seq` 参数；如果 seq 在 `pending_sequences_` 中，标记失败并渲染。
  - 会话失效错误仍清理会话，并清空 Outbox。
- 超时：
  - 启动 1 秒 `QTimer`；`onSendTimeout` 调用 `outbox_.expireSending(now, 10)`，对返回的 id 调 `message_store_.markFailed` 并渲染。
- 重发：
  - `chat_view_` 设置 `setOpenLinks(false)`，连接 `anchorClicked` 解析 `retry:<uuid>`，调用 `retryMessage`。
  - `retryMessage` 从 Outbox 找到消息，`outbox_.markSending(id, now)` 和 `message_store_.markSending(id)`，重新发送原 `client_msg_id`。
- 上下文切换：
  - `selectRoom` / `selectPrivate` 后，把 Outbox 中属于当前上下文的 Sending/Failed 消息重新加入 MessageStore，保证切换回来仍能看到。
- 断线：
  - `onDisconnected` 调 `outbox_.markAllSendingFailed()` 和 `message_store_.markAllSendingFailed()`，清空 `pending_sequences_`，渲染。
- 登出/会话失效：
  - 清空 Outbox、MessageStore、pending_sequences_。

**Steps:**
- [x] 改 `sendFrame` 返回 bool，并更新所有调用点。
- [x] 实现 `onSendClicked`、`sendMessage`、成功响应、错误关联。
- [x] 加 `send_timeout_timer_` 和 `onSendTimeout`。
- [x] 连接 `chat_view_->anchorClicked`，实现 `retryMessage`。
- [x] 上下文切换恢复当前会话的 Outbox 消息。
- [x] 编译应用：

Run: `.\build_client.ps1`
Expected: 构建成功。

- [x] **手工验收**：
  - 在线发送：显示“发送中”，随后变“已发送”。
  - 断开服务器后发送：显示“发送失败”和“重发”；恢复连接后点重发，只出现一条，状态变“已发送”。
  - 10 秒不响应：自动变失败。
  - 切换上下文再回来：失败消息仍在。

---

### Task 6: 连接横幅、离线状态与登录错误

**Files:**
- Modify: `client/ui/main_window.h`
- Modify: `client/ui/main_window.cpp`

**Interfaces:**
- 新增成员：
  ```cpp
  QLabel* connection_banner_ = nullptr;
  bool online_ = false;
  void setOnline(bool online, const QString& text = QString());
  ```
- `buildChatPage` 在上下文标题上方加入 `connection_banner_`，默认隐藏。
- `setOnline(false, text)`：
  - 显示横幅（黄色“正在连接/恢复会话”或红色“连接已断开”）。
  - 禁用 `input_edit_` 和 `send_button_`。
- `setOnline(true)`：
  - 隐藏横幅。
  - 如果有当前上下文，启用输入和发送。
- `onConnected`：先 `setOnline(false, "已连接，正在恢复会话...")`。
- 登录成功 / RESUME 成功：`setOnline(true)`。
- `onDisconnected`：
  - 已登录：保留聊天页，`setOnline(false, "连接已断开，正在重连...")`，再 `scheduleReconnect`。
  - 未登录：回登录页，错误显示在登录页。
- `handleError`：
  - 未登录时调用 `login_page_->setStatus(message, true)` 和 `setBusy(false)`。
  - 已登录时保留底部状态栏提示。
- `selectRoom` / `selectPrivate`：只有 `online_` 为 true 时启用输入。

**Steps:**
- [x] 加入横幅控件和 `setOnline`。
- [x] 更新 `onConnected`、登录/RESUME 成功、`onDisconnected`。
- [x] 更新 `handleError` 的登录页错误显示。
- [x] 更新上下文切换后的输入启用状态。
- [x] 编译应用：

Run: `.\build_client.ps1`
Expected: 构建成功。

- [x] **手工验收**：
  - 登录失败：登录页显示错误，按钮恢复可用。
  - 登录后停掉测试服务：聊天页出现红色横幅，输入/发送禁用，不退回登录页。
  - 服务恢复：横幅消失，输入恢复，自动 RESUME 成功。

---

### Task 7: 最终构建、测试、文档与验收

**Files:**
- Modify: `client/README.md`
- Modify: `client/build_client.ps1`（仅在需要时加入测试入口）
- Create: `client/screenshot-chat-usability.png`（手工验收截图）

**Steps:**
- [x] 运行客户端单元测试：

```powershell
cd D:\OpenCode_WorkSpace\chatroom\client\tests
.\run_client_tests.ps1
```
Expected: 全部测试通过。

- [x] 构建应用：

```powershell
cd D:\OpenCode_WorkSpace\chatroom\client
.\build_client.ps1
```
Expected: `Build complete: ...\build\release\chatroom_client.exe`。

- [x] 启动一个无代理测试实例或使用现有 9102 测试服务，完成 Task 4/5/6 的手工验收；保存截图。
- [x] 更新 `client/README.md`：
  - 删除“暂无未读数”的过期说明；
  - 增加消息状态、失败重发、聊天渲染、本地时间、连接横幅；
  - 增加客户端测试命令；
  - 记录“Outbox 仅进程内、不跨重启”限制。
- [x] 最后再运行一次测试和应用构建。
- [x] 在 `docs/superpowers/specs/2026-10-18-client-usability-audit.md` 末尾补验收结果表（通过/未通过 + 截图路径）。

---

## 后续阶段（不在本计划实施）

- **P1：** 用户搜索/联系人、设置页、客户端日志、托盘菜单、自动登录取消、Token 加密、心跳超时。
- **P2：** 图片/文件（HTTP 上传）、引用回复、@提及、撤回、搜索。
- **P3：** 自建群/群管理、已读/正在输入、离线推送、多端同步、管理后台。

---

## 实施结果（2026-10-18）

| 检查项 | 结果 |
|---|---|
| `run_client_tests.ps1` | 15 passed, 0 failed |
| `build_client.ps1` | 成功 |
| 在线发送 | 发送中 → 已发送 |
| 断网发送 | 失败 + 重发链接 |
| 恢复后重发 | 成功，无重复气泡 |
| 断线横幅/输入禁用 | 通过 |
| 登录错误中文提示 | 通过 |
| 历史 UTC → 本地时间 | 通过 |

截图见 `client/screenshot-*.png`，验收汇总见审计文档第 7 节。

已知限制：

- Outbox 仅进程内，应用重启后未发送/失败消息丢失。
- UI 粘合层为手工验收，未做 Qt GUI 自动化测试。
- `QTextBrowser` 全量重绘，长会话性能后续需要虚拟列表。
