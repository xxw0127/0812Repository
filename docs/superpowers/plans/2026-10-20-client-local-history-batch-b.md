# ChatBox 客户端本地历史与离线模式（批次 B）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把已确认的聊天消息写入本机 SQLite，让客户端在服务器不可用时仍能只读浏览、翻页和搜索本地记录，并让存储目录与清理操作可控。

**Architecture:** 新增 `HistoryStore`（QtSql/QSQLITE）承载建库、读写、清理与目录迁移；`MessageStore` 以可选指针写穿透，未注入时行为不变；`MainWindow` 只负责按账号打开/关闭、首屏本地预渲染、离线状态机与设置页交互。离线模式永远只读，在线时服务器历史仍是唯一权威。

**Tech Stack:** Qt 5.15.2 / MinGW 8.1 / C++17 / QtTest / QtSql（QSQLITE）/ PowerShell 构建脚本。

**Spec:** `docs/superpowers/specs/2026-10-20-client-ux-and-local-history-design.md`

## Global Constraints

- 只改 Windows Qt 客户端；服务端、协议、数据库表结构零改动。
- 不引入第三方库；SQLite 只用 QtSql，驱动为 `QSQLITE`。
- 只持久化服务端已确认消息：`message_id > 0` 且 `status == kSent`；待发/失败消息永不落盘。
- 本地库按账号隔离：所有读写 SQL 必须带 `user_id`；连接名 `history_<user_id>`。
- 每页固定 50 条；首屏只渲染最近 50 条。
- 默认目录：`QStandardPaths::AppDataLocation + "/history"`；设置键 `settings/history/path`；数据库文件名 `history.sqlite`。
- 清理只删本机副本，绝不影响服务器历史；在线清理后当前视图保持不动，离线清理后当前会话与侧栏立即重算。
- 离线模式只读：不发送任何帧，不提供离线发送/队列；`--minimized`、单实例锁、托盘、通知开关等批次 A 行为不得改动。
- 代码风格沿用现有客户端：4 空格、K&R、类名大驼峰、函数小驼峰、固定宽度类型 `quint64/qint64`、头文件用 `#ifndef/#define/#endif`、面向用户的文案为中文。
- 编译要求：`-Wall -Wextra` 下无新增警告；每次任务结束运行 `tests\run_client_tests.ps1`。
- 日志不得包含消息正文、密码或 token；只允许记录消息 id、会话 id、错误文本。
- 在功能分支上提交，不推送、不合并 `main`；集成留到批次结束后按 finishing 流程处理。

## Review Focus

1. **便携包缺少 `sqldrivers/qsqlite.dll` 或 `Qt5Sql.dll`**：客户端必须照常启动、聊天可用，历史功能降级并在日志/状态栏给出可理解的失败，而不是崩溃或白屏。
2. **乱序历史页与重复写入**：同一消息重复写入不得产生第二行；更早的页不得把 `conversations.last_*` 往回改；撤回后读回必须是撤回态。
3. **切换目录失败（目标不可写、目标已有库、复制/校验失败）**：必须中止切换并保持旧库仍可用，不能出现“设置已保存但数据打不开”。
4. **离线模式误发帧 / 退出登录后无法再看本地记录**：离线浏览不得触发登录、RESUME 以外的任何请求；退出登录保留本地库与 `history/last_user_id`，登录页仍能进入本地记录。
5. **搜索输入 `%`、`_`、`'` 与跨会话泄漏**：LIKE 必须转义并绑定参数；搜索结果只能来自当前用户、当前会话。

---

### Task 1: HistoryStore 骨架、建库与打开/关闭

**Files:**
- Create: `chatbox/client/core/history_store.h`
- Create: `chatbox/client/core/history_store.cpp`
- Modify: `chatbox/client/chatbox.pro`（`QT += sql`，新增源文件/头文件）
- Modify: `chatbox/client/tests/tests.pro`（`QT += sql`，新增源文件/头文件）
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: `ChatContext`, `ChatMessage`（`core/chat_message.h`）
- Produces:
  ```cpp
  struct ConversationRow {
      ChatContext context = ChatContext::kRoom;
      quint64 target_id = 0;
      QString title;
      quint64 last_message_id = 0;
      QString last_content;
      QDateTime last_time_utc;
  };

  class HistoryStore {
  public:
      ~HistoryStore();
      bool open(const QString& directory, quint64 user_id, QString* error);
      void close();
      bool isOpen() const;
      QString directory() const;      // 绝对路径
      quint64 userId() const;
      qint64 sizeBytes() const;       // history.sqlite + -wal + -shm
      qint64 messageCount() const;    // 当前 user_id 的消息数
      bool hasMessages() const;
      static QString defaultDirectory();
  private:
      // connection_name_ = "history_<user_id>"；测试通过该名字检查 schema
  };
  ```
- 打开契约：目录不存在则 `mkpath`；数据库文件固定 `history.sqlite`；`PRAGMA journal_mode=WAL`、`synchronous=NORMAL`、`user_version=1`；重复 `open` 同一目录+账号直接返回 true；换账号/换目录先 `close()`。

- [ ] **Step 1: 写失败测试**

在 `test_client_core.cpp` 顶部加入 `#include <QSqlDatabase>`、`#include <QSqlQuery>`、`#include <QStringList>`、`#include "core/history_store.h"`，在 `private slots` 末尾加入两个槽，然后实现：

```cpp
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

    QVERIFY(store.open(dir.path(), 1101, &error));  // 幂等
    store.close();
    QVERIFY(!store.isOpen());
}

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
```

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 编译失败，`core/history_store.h` 不存在。

- [ ] **Step 3: 实现建库与打开/关闭**

`history_store.cpp` 的 `open()`：校验 `user_id != 0`；`QDir(directory).absolutePath()`；`QDir().mkpath()`；若当前已打开同目录同账号直接返回 true，否则 `close()`；`QSqlDatabase::addDatabase("QSQLITE", "history_<id>")` 前先清理同名残留连接；设置数据库文件名并 `open()`；顺序执行：

```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
CREATE TABLE IF NOT EXISTS messages (...);          -- 与 spec 5.2 完全一致
CREATE UNIQUE INDEX IF NOT EXISTS uk_msg_server ...;
CREATE INDEX IF NOT EXISTS idx_msg_page ...;
CREATE TABLE IF NOT EXISTS conversations (...);     -- 与 spec 5.2 完全一致
PRAGMA user_version=1;
```

失败时写入中文错误文本到 `*error`、关闭并移除连接、返回 false。`close()` 先销毁所有 `QSqlQuery` 作用域，再 `db.close()`、`QSqlDatabase()`、`QSqlDatabase::removeDatabase(name)`。`sizeBytes()` 累加 `history.sqlite`、`history.sqlite-wal`、`history.sqlite-shm`。`defaultDirectory()` 返回 `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/history"`。

- [ ] **Step 4: 运行测试确认通过**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 56 passed / 0 failed。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/core/history_store.h chatbox/client/core/history_store.cpp \
        chatbox/client/chatbox.pro chatbox/client/tests/tests.pro \
        chatbox/client/tests/test_client_core.cpp
git commit -m "feat(history): add SQLite history store and schema"
```

---

### Task 2: 消息写入、去重、读取与本地搜索

**Files:**
- Modify: `chatbox/client/core/history_store.h`
- Modify: `chatbox/client/core/history_store.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: Task 1 的 `HistoryStore` 与 schema。
- Produces:
  ```cpp
  bool saveMessage(const ChatMessage& message);  // message_id == 0 返回 false 且不写
  bool markRecalled(quint64 message_id);
  QVector<ChatMessage> recent(ChatContext context, quint64 target_id, int limit) const;
  QVector<ChatMessage> before(ChatContext context, quint64 target_id,
                              quint64 before_id, int limit) const;
  QVector<ChatMessage> search(ChatContext context, quint64 target_id,
                              const QString& keyword, int limit) const;
  ```
- 约定：`recent`/`before` 升序返回（旧→新）；`search` 按 `message_id DESC` 返回；`limit` 夹到 `1..200`；时间列存 `yyyy-MM-dd HH:mm:ss` UTC（服务端同格式，可直接字符串比较）。

- [ ] **Step 1: 写失败测试**

在匿名命名空间加入助手：

```cpp
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
```

加入 7 个测试：

```cpp
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

void TestClientCore::historyStoreDedupesByMessageId() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1202, &error));
    QVERIFY(store.saveMessage(confirmedMessage(5, 7, "hello")));
    QVERIFY(store.saveMessage(confirmedMessage(5, 7, "edited")));
    QCOMPARE(store.messageCount(), qint64(1));
    QCOMPARE(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).at(0).content,
             QString("edited"));
}

void TestClientCore::historyStoreSkipsUnconfirmedMessages() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1203, &error));
    auto pending = confirmedMessage(0, 7, "not confirmed");
    pending.status = chatbox::client::MessageStatus::kSending;
    QVERIFY(!store.saveMessage(pending));
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(!store.hasMessages());
}

void TestClientCore::historyStoreMarksRecalled() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1204, &error));
    QVERIFY(store.saveMessage(confirmedMessage(7, 7, "to be recalled")));
    QVERIFY(store.markRecalled(7));
    const auto rows = store.recent(chatbox::client::ChatContext::kRoom, 7, 10);
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows.at(0).recalled);
    QVERIFY(rows.at(0).content.isEmpty());
    QVERIFY(!store.markRecalled(999));
}

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
```

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 编译失败，`saveMessage/recent/...` 未声明。

- [ ] **Step 3: 实现读写与搜索**

- `saveMessage`：拒绝 `message_id == 0`；UPSERT（`INSERT ... ON CONFLICT(user_id, context, target_id, message_id) DO UPDATE`）写入全部列；`created_at_utc` 非法时用当前 UTC；`recalled` 时内容写空。
- `recent`：`WHERE user_id=? AND context=? AND target_id=? ORDER BY message_id DESC LIMIT ?`，再反转为升序。
- `before`：同上加 `message_id < ?`。
- `markRecalled`：`UPDATE messages SET recalled=1, content='' WHERE user_id=? AND message_id=?`；影响 0 行返回 false。
- `search`：`keyword` 先转义 `\`、`%`、`_`，SQL 用 `content LIKE ? ESCAPE '\'` 绑定 `%escaped%`，限定 user/context/target，`ORDER BY message_id DESC LIMIT ?`。
- 所有 `QSqlQuery` 必须在函数作用域内销毁后再关闭数据库。

- [ ] **Step 4: 运行测试确认通过**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 63 passed / 0 failed。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/core/history_store.h chatbox/client/core/history_store.cpp \
        chatbox/client/tests/test_client_core.cpp
git commit -m "feat(history): persist, dedupe and search confirmed messages"
```

---

### Task 3: 会话摘要、清理与目录迁移

**Files:**
- Modify: `chatbox/client/core/history_store.h`
- Modify: `chatbox/client/core/history_store.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  bool saveConversation(const ConversationRow& row);
  QVector<ConversationRow> conversations() const;  // last_message_id DESC，忽略空会话
  bool clearAll();                                 // 只清当前 user_id
  bool clearOlderThan(int days);                   // 1..365
  bool moveTo(const QString& new_directory, bool migrate, QString* error);
  static bool ensureWritableDirectory(const QString& directory, QString* error);
  ```
- 规则：`saveMessage` 只把 `conversations.last_*` 向更新方向推进（`new_id >= last_id` 时更新，等号允许覆盖内容/撤回态）；`saveConversation` 只补充/更新 `title` 并重算 `last_*`；`clearOlderThan` 删除过期消息后重算全部会话，`last_message_id == 0` 的会话删除；`moveTo` 要求已打开且目标 `history.sqlite` 不存在，迁移时复制主库/`-wal`/`-shm` 并用 `messageCount()` 校验行数，失败则重新打开旧目录并返回 false。

- [ ] **Step 1: 写失败测试**

```cpp
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
```

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 编译失败，新方法未声明。

- [ ] **Step 3: 实现摘要、清理与迁移**

- `saveConversation`：`INSERT OR IGNORE` 保证行存在；`title` 非空才更新；随后按最新消息重算 `last_*`（无消息则保持 0）。
- `saveMessage` 内联更新会话：先读当前 `last_message_id`，仅当 `message_id >= current` 时更新 `last_content/last_time/last_message_id`。
- `clearOlderThan`：计算 UTC 截止串，`DELETE FROM messages WHERE user_id=? AND created_at_utc < ?`；遍历该用户全部 `(context,target)` 调 `refreshConversationRow`；删除 `last_message_id=0` 的会话；返回 false 仅限 SQL 失败。
- `conversations()`：读列并组装 `ConversationRow`；`title` 为空时房间回退 `"房间 <target>"`、私聊回退 `"用户 <target>"`。
- `moveTo`：`!isOpen()` 或目标库已存在 → 错误；`close()`；`migrate` 为真时复制三个文件；`open(new_dir, user_id_)`；`messageCount()` 与迁移前不一致 → 删除目标副本、重开旧目录、返回 false；成功后 `directory_` 已是新目录。`ensureWritableDirectory` 用 `mkpath` + 临时文件写入/删除验证。

- [ ] **Step 4: 运行测试确认通过**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 67 passed / 0 failed。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/core/history_store.h chatbox/client/core/history_store.cpp \
        chatbox/client/tests/test_client_core.cpp
git commit -m "feat(history): add conversation summaries, cleanup and directory move"
```

---

### Task 4: MessageStore 写穿透

**Files:**
- Modify: `chatbox/client/core/message_store.h`
- Modify: `chatbox/client/core/message_store.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: `HistoryStore::saveMessage`, `markRecalled`。
- Produces: `void MessageStore::setHistoryStore(HistoryStore* store);`（不拥有指针；`nullptr` 表示纯内存模式）。
- 写穿透点：`replaceHistory`、`prependHistory`、`appendIncoming`、`markSent`、`markRecalled`；仅在 `message_id > 0 && status == kSent` 时调用；写库失败只记录日志，不影响内存与 UI。

- [ ] **Step 1: 写失败测试**

```cpp
void TestClientCore::messageStoreWritesThroughWhenHistoryAttached() {
    QTemporaryDir dir; chatbox::client::HistoryStore history; QString error;
    QVERIFY(history.open(dir.path(), 1401, &error));

    chatbox::client::MessageStore store;
    store.setHistoryStore(&history);
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
```

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 编译失败，`setHistoryStore` 未声明。

- [ ] **Step 3: 实现写穿透**

头文件前置声明 `class HistoryStore;`，增加私有 `HistoryStore* history_ = nullptr;` 与 `void persistMessage(const ChatMessage&) const;`。实现：

```cpp
void MessageStore::persistMessage(const ChatMessage& message) const {
    if (history_ == nullptr || message.message_id == 0 ||
        message.status != MessageStatus::kSent) {
        return;
    }
    if (!history_->saveMessage(message)) {
        qWarning("history write failed message_id=%llu",
                 static_cast<unsigned long long>(message.message_id));
    }
}
```

`replaceHistory`/`prependHistory` 末尾遍历最终 `messages_` 调 `persistMessage`；`appendIncoming` 在成功 push 后调用；`markSent` 在去重与状态更新后调用（此时已带服务端 id）；`markRecalled` 先在内存置位，再调 `history_->markRecalled(message_id)`。

- [ ] **Step 4: 运行测试确认通过**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 68 passed / 0 failed。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/core/message_store.h chatbox/client/core/message_store.cpp \
        chatbox/client/tests/test_client_core.cpp
git commit -m "feat(history): write confirmed messages through MessageStore"
```

---

### Task 5: 构建与便携包补齐 SQLite 驱动

**Files:**
- Modify: `chatbox/client/build_client.ps1`
- Modify: `chatbox/client/package_client.ps1`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces: `build\release\Qt5Sql.dll`、`build\release\sqldrivers\qsqlite.dll`；ZIP 内包含 `imageformats`、`sqldrivers`。

- [ ] **Step 1: 写失败测试**

```cpp
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
```

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 1 failed（脚本尚未包含驱动）。

- [ ] **Step 3: 修改构建与打包脚本**

`build_client.ps1`：
- DLL 列表加入 `Qt5Sql.dll`。
- 新增 `sqldrivers` 目录，复制 `$QtDir\plugins\sqldrivers\qsqlite.dll`；缺失时 `Write-Warning`，不中断构建。

`package_client.ps1`：
- 目录列表改为 `@("certs", "platforms", "styles", "imageformats", "sqldrivers")`，统一复制。
- 打包完成后校验目标目录存在 `chatbox.exe`、`Qt5Sql.dll`、`sqldrivers\qsqlite.dll`、`imageformats\qjpeg.dll`，缺失则 `throw`。

- [ ] **Step 4: 构建并检查产物**

Run: `cd chatbox\client; .\build_client.ps1`
然后：

```powershell
Test-Path build\release\Qt5Sql.dll
Test-Path build\release\sqldrivers\qsqlite.dll
```

Expected: 两行均 `True`；随后运行 `tests\run_client_tests.ps1`，69 passed / 0 failed。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/build_client.ps1 chatbox/client/package_client.ps1 \
        chatbox/client/tests/test_client_core.cpp
git commit -m "build: ship Qt SQL and SQLite driver in portable package"
```

---

### Task 6: 会话首屏本地预渲染 + 任意会话推送落盘

**Files:**
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: `HistoryStore`、`MessageStore::setHistoryStore`。
- Produces（MainWindow 私有）:
  ```cpp
  bool openHistoryStore(quint64 user_id);
  void closeHistoryStore();
  void loadInitialHistory(chatbox::client::ChatContext context, quint64 target_id);
  ```
- 行为：
  - `openHistoryStore` 读 `settings/history/path`（缺省 `HistoryStore::defaultDirectory()`），打开当前账号并 `message_store_.setHistoryStore(&history_store_)`；失败只记日志/状态，聊天照常。
  - LOGIN/RESUME 成功后调用；`clearSession()` 与析构先 `setHistoryStore(nullptr)` 再 `close()`。
  - `selectRoom`/`selectPrivate`：`message_store_.reset()` 后调用 `loadInitialHistory()`（`recent(..., 50)`，非空则 `replaceHistory` 并写穿透），再恢复 Outbox、渲染；在线仍照旧 `requestHistory` 覆盖。
  - `kCmdChatPush`、`kCmdPrivatePush`：解析出消息后立即 `history_store_.saveMessage(...)`（不论是否为当前会话），当前会话再走 `addIncomingMessage`（重复写入由唯一索引吸收）。
  - 会话标题：`kCmdRoomList`、`selectRoom`、`selectPrivate`、`kCmdPrivateConversations`、`kCmdPrivatePush` 调 `saveConversation()`（私聊备注优先、否则昵称）。

- [ ] **Step 1: 写失败测试**

```cpp
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

void TestClientCore::incomingPushesArePersistedForOtherConversations() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    const QString room_push = caseSection(source, QStringLiteral("case chatbox::kCmdChatPush:"));
    const QString private_push = caseSection(source, QStringLiteral("case chatbox::kCmdPrivatePush:"));
    QVERIFY(room_push.contains(QStringLiteral("history_store_.saveMessage(")));
    QVERIFY(private_push.contains(QStringLiteral("history_store_.saveMessage(")));
    QVERIFY(private_push.contains(QStringLiteral("saveConversation(")));
}
```

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 2 failed（函数/调用不存在）。

- [ ] **Step 3: 实现生命周期、预渲染与推送落盘**

- 头文件加 `#include "core/history_store.h"`、成员 `chatbox::client::HistoryStore history_store_;`。
- `openHistoryStore` 幂等；`closeHistoryStore` 先解绑 MessageStore。
- `loadInitialHistory` 只在 `history_store_.isOpen()` 时工作。
- `selectRoom`/`selectPrivate` 内调整为：`reset` → `loadInitialHistory` → `restorePendingForCurrentContext` → 渲染。
- 推送分支：构造 `ChatMessage` 后先保存再判断是否当前会话；私聊 push 中补 `saveConversation({kPrivate, from_id, nickname})`。
- `kCmdRoomList` 遍历时（包括被 UI 跳过的房间）保存 `{kRoom, id, name}`；`kCmdPrivateConversations` 保存昵称（`friend_views_` 中已有备注则用备注）。
- `clearSession()` 与 `~MainWindow()` 关闭库；`clearSession()` 不删除 `history/*` 键。

- [ ] **Step 4: 运行测试并通过在线取证**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 71 passed / 0 failed。

随后启动客户端，打开一个房间和一个私聊，各收发一条消息，关闭客户端后执行：

```powershell
python -c "import sqlite3,os; p=os.path.expandvars(r'%APPDATA%\ChatBox\ChatBox\history\history.sqlite'); c=sqlite3.connect(p); print('messages', c.execute('select count(*) from messages').fetchone()[0]); print(c.execute('select context,target_id,title,last_message_id from conversations').fetchall())"
```

Expected: `messages` ≥ 3；`conversations` 同时出现房间与私聊行，`title` 非空。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/ui/main_window.h chatbox/client/ui/main_window.cpp \
        chatbox/client/tests/test_client_core.cpp
git commit -m "feat(history): render local history first and persist background pushes"
```

---

### Task 7: 离线模式、登录页本地入口与本地搜索

**Files:**
- Modify: `chatbox/client/ui/login_page.h`
- Modify: `chatbox/client/ui/login_page.cpp`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window.cpp`
- Modify: `chatbox/client/tests/tests.pro`（SOURCES 加入 `../ui/login_page.cpp`，HEADERS 加入 `../ui/login_page.h`）
- Test: `chatbox/client/tests/test_client_core.cpp`（新增 `#include "ui/login_page.h"`）

**Interfaces:**
- LoginPage：
  ```cpp
  void setLocalHistoryAvailable(bool available, const QString& nickname = QString());
  signals: void viewLocalHistoryRequested();
  ```
  按钮 objectName `LocalHistoryButton`，文案 `查看本地记录`（有昵称时 `查看本地记录（昵称）`），默认隐藏。
- MainWindow：
  ```cpp
  bool enterOfflineMode(bool with_token);
  void leaveOfflineMode();
  void populateOfflineSidebar();
  void loadOlderFromLocal();
  void retryConnection();      // 槽
  void openLocalHistory();     // 槽
  void updateLocalHistoryEntry();
  ```
  成员：`bool offline_mode_ = false; bool offline_session_ = false;`、`QWidget* banner_row_`、`QPushButton* reconnect_button_`。
- 状态机：`loadSession()` 先 `openHistoryStore(user_id_)`；RESUME/LOGIN 失败且 `hasMessages()` → 进入离线；离线横幅文案固定 `离线模式 · 仅显示本地记录`，右侧【重试连接】（无 token 的本地浏览隐藏）；自动重连定时器继续跑；RESUME/LOGIN 成功后 `leaveOfflineMode()`。
- 离线约束：`setOnline(false)` 时禁用/隐藏 `@`、成员列表、好友申请、搜索用户、资料、撤回/重发；`sendRecall` 首行必须是 `if (!online_) { setStatus("当前离线，无法撤回", true); return; }`；`openProfile`、`openOwnProfile`、`openFriendRequests`、`openUserSearch` 早退；`onSendClicked` 保留 `!online_` 早退。
- 失败回退：有 token 但本地无该账号数据时，不进入离线模式，停留在登录页显示连接失败，同时保留指数退避重连。
- 离线选择会话：跳过 JOIN/成员/服务端 HISTORY，改用 `recent` + `before` 分页；`has_more_`/`next_before_id_` 由本地库决定。
- 离线搜索：`requestMessageSearch` 走 `history_store_.search(context, target, keyword, 50)`，转换 `MessageSearchResult` 并标注 `本地记录`。
- 退出登录：`offline_mode_` 时直接 `finishLogout()`；`saveSession()` 持久化 `history/last_user_id`、`history/last_nickname`；`loadSession()` 读到已保存会话时也立即回填这两个键（覆盖升级前只有 `session/*` 的用户）；`clearSession()` 保留这两个键；登录页据此显示本地入口。`openLocalHistory()` 在 `history/last_nickname` 为空时回退读 `session/nickname`。

- [ ] **Step 1: 写失败测试**

```cpp
void TestClientCore::loginPageOffersLocalHistory() {
    LoginPage page;
    auto* button = page.findChild<QPushButton*>(QStringLiteral("LocalHistoryButton"));
    QVERIFY(button != nullptr);
    QVERIFY(button->isHidden());  // isVisible() is false while the page is not shown
    page.setLocalHistoryAvailable(true, QStringLiteral("alice"));
    QVERIFY(!button->isHidden());
    QVERIFY(button->text().contains(QStringLiteral("alice")));
    QSignalSpy spy(&page, &LoginPage::viewLocalHistoryRequested);
    button->click();
    QCOMPARE(spy.count(), 1);
    page.setLocalHistoryAvailable(false);
    QVERIFY(button->isHidden());
}

void TestClientCore::offlineModeIsReadOnlyAndLocal() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY(source.contains(QStringLiteral("enterOfflineMode(")));
    QVERIFY(source.contains(QStringLiteral("离线模式 · 仅显示本地记录")));
    QVERIFY(source.contains(QStringLiteral("viewLocalHistoryRequested")));
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
```

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 编译失败（`LoginPage` 未加入测试工程 / 新 API 不存在）。

- [ ] **Step 3: 实现离线状态机与本地入口**

- LoginPage：在按钮区下方加 `local_history_button_`，`setLocalHistoryAvailable` 控制可见与文案；点击发信号。
- 头部横幅改为 `banner_row_ = QWidget`（`QHBoxLayout`：`connection_banner_` + `reconnect_button_`），`setOnline` 控制整行显隐；`reconnect_button_` 仅 `offline_mode_ && offline_session_` 时显示，点击 `retryConnection()`。
- `loadSession()` 打开历史库后连接；`onDisconnected`/`onAuthTimeout` 的 RESUME 分支先尝试 `enterOfflineMode(true)`，失败才回登录页；两种分支都保留 `scheduleReconnect()`。
- `enterOfflineMode`：必要时打开库并校验 `hasMessages()`；置位 `offline_mode_`/`offline_session_`；切到 `chat_page_`；`setOnline(false, ...)`；仅在 `context_type_ == None` 时 `populateOfflineSidebar()` 并选中第一条；状态栏提示。
- `populateOfflineSidebar`：房间行来自 `conversations()` 的 kRoom；私聊写入 `friend_views_`（`online=false`、`unread=0`、`last_content/last_time`）后 `refreshFriendListView()`；清空时 `resetContext()`。
- `loadInitialHistory` 增补离线分页：`has_more_ = !before(..., oldest, 1).isEmpty()`，`next_before_id_ = oldest`，同步 `load_more_action_`。
- `onLoadMoreClicked`：`offline_mode_` → `loadOlderFromLocal()`；否则保持服务端翻页。
- `requestMessageSearch`：先做 64 字节校验，再分支本地搜索；`created_at` 用 `yyyy-MM-dd HH:mm:ss` 回填。
- 登录页信号连接 `openLocalHistory`：读取 `history/last_user_id`，打开库并检查 `hasMessages()`，设置 `user_id_/nickname_`、清空 token、`logged_in_ = false`，进入离线；无数据显示状态。
- `clearSession()`/构造后调用 `updateLocalHistoryEntry()`；`saveSession()` 写 last_user_id/last_nickname。
- `setOnline`：按 `offline_mode_` 决定横幅文案不被重连消息覆盖，并统一禁用服务型按钮与 `mention_button_`（离线或未连接）。
- `sendRecall`、`openProfile`、`openOwnProfile`、`openFriendRequests`、`openUserSearch`、`onFriendContextMenu` 增加离线/未连接早退；`showMessageMenu` 离线不提供撤回。

- [ ] **Step 4: 运行测试并做停机演练**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 73 passed / 0 failed。

停机演练（先确保本机已有历史）：

```powershell
ssh chatroom-dev "sudo systemctl stop chatbox"
# 重启客户端，确认：离线横幅 + 重试按钮、左栏本地会话、可切换/翻页/搜索、输入区禁用
ssh chatroom-dev "sudo systemctl start chatbox"
# 10 秒内自动 RESUME，横幅消失，可正常发送
```

Expected: 上述行为全部成立；离线期间服务端日志没有新的客户端帧（除重连尝试）。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/ui/login_page.h chatbox/client/ui/login_page.cpp \
        chatbox/client/ui/main_window.h chatbox/client/ui/main_window.cpp \
        chatbox/client/tests/tests.pro chatbox/client/tests/test_client_core.cpp
git commit -m "feat(history): add read-only offline mode and local search"
```

---

### Task 8: 设置页存储位置、大小与清理

**Files:**
- Modify: `chatbox/client/ui/settings_dialog.h`
- Modify: `chatbox/client/ui/settings_dialog.cpp`
- Modify: `chatbox/client/ui/main_window.cpp`
- Modify: `chatbox/client/tests/tests.pro`（SOURCES 加入 `../ui/settings_dialog.cpp`、`../core/client_log.cpp`，HEADERS 加入对应头文件）
- Test: `chatbox/client/tests/test_client_core.cpp`（新增 `#include "ui/settings_dialog.h"`）

**Interfaces:**
- SettingsDialog 构造：
  ```cpp
  SettingsDialog(const QString& public_id, const QString& history_directory,
                 qint64 history_size_bytes, bool history_available,
                 QWidget* parent = nullptr);
  void setHistoryInfo(const QString& directory, qint64 size_bytes, bool available);
  signals:
      void historyPathChangeRequested(const QString& directory, bool migrate);
      void historyClearAllRequested();
      void historyClearOlderRequested(int days);
  ```
- objectName：`HistoryPathLabel`、`HistorySizeLabel`、`ChangeHistoryDirButton`、`OpenHistoryDirButton`、`ClearHistoryButton`。
- 大小文案：`< 1024` → `N B`；`< 1 MiB` → `%.1f KB`；否则 `%.1f MB`（`4096` → `4.0 KB`）。实现为 `settings_dialog.cpp` 匿名命名空间的 `QString formatHistorySize(qint64 bytes)`。
- 清理对话框：两项单选（清空全部 / 清理 N 天前，`QSpinBox` 1–365 默认 30）+ 确认勾选「只删除本机副本，不影响服务器历史」；未勾选不发射信号。
- MainWindow：设置页打开时传入 `history_store_.directory()/sizeBytes()/isOpen()`；改目录先 `ensureWritableDirectory`，再 `moveTo(dir, migrate)`，成功后写 `settings/history/path` 并 `setHistoryInfo`；清理后刷新信息；离线时清理后调用 `void refreshOfflineAfterClear();`（重置当前会话、按本地库重载、重建侧栏；无剩余会话则 `resetContext()`），在线时视图不动。

- [ ] **Step 1: 写失败测试**

```cpp
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
```

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 编译失败（新构造参数/控件不存在）。

- [ ] **Step 3: 实现设置页与 MainWindow 应用逻辑**

- 设置页高度调到约 `520x560`；在日志区上方插入「本地聊天记录」分组：路径只读 `QLineEdit`（或可选中 `QLabel`）、大小标签、三个按钮。
- 更改目录：`QFileDialog::getExistingDirectory`；与当前相同则忽略；弹「是否迁移现有记录？」（是/否/取消），是→`migrate=true`；发射信号。
- 清理：内联 `QDialog`（radio ×2 + spin + 确认框 + OK/Cancel），只发射选中的信号；确认框文案含「只删除本机副本，不影响服务器历史」。
- MainWindow 的 `openSettings` 传入历史信息并连接三个信号；改目录失败用 `QMessageBox::warning` 显示错误且不改设置键；成功后更新对话框与状态栏。清理成功调用 `setHistoryInfo`；离线时 `refreshOfflineAfterClear()`（重置当前会话、重载、重建侧栏）。
- `history_available == false` 时路径显示「本地历史不可用（缺少 SQLite 驱动）」，三个按钮禁用。

- [ ] **Step 4: 运行测试并手动验收**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 75 passed / 0 failed。

手动：
1. 设置页看到当前目录、库大小；【打开目录】能打开资源管理器。
2. 换到新目录并选「迁移」→ 关闭重开客户端 → 新目录有 `history.sqlite`，旧会话可读。
3. 换目录选「不迁移」→ 新库为空，旧目录文件仍在。
4. 清理全部 → 在线当前视图不动、侧栏保留；离线清空后当前视图与侧栏立即清空；重开在线会话能从服务器重新拉回并落盘。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/ui/settings_dialog.h chatbox/client/ui/settings_dialog.cpp \
        chatbox/client/ui/main_window.cpp chatbox/client/tests/tests.pro \
        chatbox/client/tests/test_client_core.cpp
git commit -m "feat(history): expose storage directory, size and cleanup in settings"
```

---

### Task 9: 文档、全量回归与便携包验收

**Files:**
- Modify: `chatbox/client/README.md`
- Modify: `README.md`
- Test: 全量回归 + 便携包检查

**Interfaces:**
- Consumes: 前 8 个任务全部产物。
- Produces: 与实现一致的客户端文档和可分发 ZIP。

- [ ] **Step 1: 更新文档**

`chatbox/client/README.md` 新增「本地聊天记录与离线模式」小节，写明：
- 默认目录 `%APPDATA%\ChatBox\ChatBox\history\history.sqlite`，可用设置页更改；每账号隔离。
- 只保存服务端已确认消息（待发/失败不落盘）；在线以服务器历史为准。
- 离线进入条件、只读限制、本地搜索范围（当前会话）、自动重连。
- 清理只删本机副本；在线重新打开会话会从服务器重新拉取并再次落盘。
- 便携包必须带 `Qt5Sql.dll`、`sqldrivers\qsqlite.dll`、`imageformats\qjpeg.dll`，缺失时历史/头像降级。
- 把测试数字改成实际输出值（预期 75）。

根 `README.md`：功能列表加入「本地历史 + 离线只读」，测试数字同步。

- [ ] **Step 2: 全量构建与测试**

Run:

```powershell
Get-Process chatbox -ErrorAction SilentlyContinue | Stop-Process -Force
cd chatbox\client
.\build_client.ps1
cd tests
.\run_client_tests.ps1
```

Expected: 构建成功；75 passed / 0 failed。

- [ ] **Step 3: 便携包内容验收**

Run: `cd chatbox\client; .\package_client.ps1 -Version 0.2.0`
然后：

```powershell
$tmp = Join-Path $env:TEMP "chatbox-pkg-check"
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive dist\chatbox-0.2.0-win64.zip -DestinationPath $tmp
Test-Path "$tmp\chatbox-0.2.0\Qt5Sql.dll"
Test-Path "$tmp\chatbox-0.2.0\sqldrivers\qsqlite.dll"
Test-Path "$tmp\chatbox-0.2.0\imageformats\qjpeg.dll"
```

Expected: 三行均 `True`。

- [ ] **Step 4: 最终冒烟**

在线启动一次、发送消息、关闭重开（本地历史可见）；停服务端后重开（离线模式）；恢复服务端（自动在线）。确认日志无 `history write failed` 与 SQL 错误。

驱动缺失降级检查（在解压出的便携包副本里做，不动 `build\release`）：

```powershell
Rename-Item "$tmp\chatbox-0.2.0\sqldrivers" "$tmp\chatbox-0.2.0\sqldrivers.bak"
& "$tmp\chatbox-0.2.0\chatbox.exe"
# 预期：应用正常启动、可登录聊天；设置页显示“本地历史不可用（缺少 SQLite 驱动）”
Rename-Item "$tmp\chatbox-0.2.0\sqldrivers.bak" "$tmp\chatbox-0.2.0\sqldrivers"
```

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/README.md README.md
git commit -m "docs: document local history, offline mode and SQLite packaging"
```

---

## 完成标准

- `run_client_tests.ps1` 全绿（预期 75）。
- 在线、离线、切目录（迁移/不迁移）、清理全部/按天、便携包五条链路均按本计划验证通过。
- 服务端仓库零改动；离线期间除重连外不产生业务请求。
- 所有提交在功能分支上，未推送、未合并 `main`。
