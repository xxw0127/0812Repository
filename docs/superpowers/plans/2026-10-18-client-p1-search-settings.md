# Client P1 Search / Contacts / Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox syntax.

**Goal:** 增加用户搜索、联系人、客户端日志、设置页、图标/版本和便携分发包。

**Architecture:** 服务端新增 USER_SEARCH 命令和 `searchUsers` DAO；客户端新增 `ContactStore`、`UserSearchDialog`、`SettingsDialog`、`client_log`。联系人/服务器/通知设置使用 QSettings 分组，退出登录只清 `session` 组。

**Tech Stack:** C++17、Qt 5.15 Widgets、MySQL 8.4、QtTest、PowerShell

**Spec:** `docs/superpowers/specs/2026-10-18-client-p1-search-settings-design.md`

**Status:** 已完成（2026-10-18）。

## Global Constraints

- 不新增第三方依赖；不安装 Inno Setup/NSIS。
- 联系人本地存储，不建服务端好友表。
- 日志不得记录密码或 Token。
- 退出登录不得清空联系人/设置。
- 服务端协议变更必须同步更新 README 和 Python 测试工具。

## Review Focus

- 搜索关键词含 `%`、`_`、引号或超长内容时不能注入 SQL，也不能返回禁用用户。
- 退出登录、重启客户端后联系人和服务器配置不能丢失。
- 搜索响应和私聊会话响应不能串到错误对话框/会话。
- 日志文件不能无限增长，不能写入敏感信息。
- 设置页修改服务器后，下次登录/重连必须使用新配置。

---

### Task 1: 服务端 USER_SEARCH

**Files:**
- Modify: `common/protocol.h`
- Modify: `server/db/mysql_connection.h/.cpp`
- Modify: `server/private/private_service.h/.cpp`
- Modify: `server/main.cpp`
- Modify: `README.md` protocol table
- Create: `tools/user_search_client.py`

**Interfaces:**
- `kCmdUserSearch = 0x0060`
- `struct UserSearchRow { uint64_t id; std::string username; std::string nickname; };`
- `bool MysqlConnection::searchUsers(const std::string& keyword, std::vector<UserSearchRow>& users, std::string& error);`
- Response: `count u16 + [user_id u64, username, nickname, online u8]`.

- [x] 写 Python 失败测试：注册两个唯一用户，登录一个，搜索另一个的用户名/昵称，断言返回 id、nickname、online=1；搜索不存在关键词返回 0。
- [x] 运行测试，确认 USER_SEARCH 未实现而失败。
- [x] 实现 DAO：`SELECT id, username, nickname FROM users WHERE status=0 AND (username LIKE ? OR nickname LIKE ?) ORDER BY username LIMIT 20`，两个参数都用 `%keyword%`。
- [x] 在 `PrivateService` 实现 `handleUserSearch`；关键词长度 1-32；响应带 online。
- [x] `main.cpp` 把 `kCmdUserSearch` 纳入 private_command。
- [x] 构建服务端并运行 ctest 4/4。
- [x] 启动 9102 测试实例，运行 `tools/user_search_client.py` 通过。

### Task 2: ContactStore

**Files:**
- Create: `client/core/contact_store.h/.cpp`
- Modify: `client/tests/tests.pro`, `client/tests/test_client_core.cpp`

**Interfaces:**
```cpp
struct Contact { quint64 user_id = 0; QString username; QString nickname; };
class ContactStore {
    QVector<Contact> contacts() const;
    bool add(const Contact& contact);
    bool remove(quint64 user_id);
};
```
- 存 `QSettings` 的 `contacts/json`（QJsonArray）。
- 按 `user_id` 去重；`add` 已存在时更新 username/nickname 并返回 false。
- 测试：add、重复 add 更新、remove、重新构造后仍能读取。
- 测试使用 `QSettings::setDefaultFormat(QSettings::IniFormat)` + 临时目录，结束后恢复。

- [x] 写失败测试。
- [x] 运行测试确认失败。
- [x] 实现 ContactStore。
- [x] 运行测试通过。

### Task 3: 用户搜索对话框与联系人 UI

**Files:**
- Create: `client/ui/user_search_dialog.h/.cpp`
- Modify: `client/ui/main_window.h/.cpp`
- Modify: `client/chatroom_client.pro`

**Interfaces:**
- `UserSearchDialog::searchRequested(QString keyword)`
- `UserSearchDialog::userActivated(quint64 user_id, QString username, QString nickname)`
- `UserSearchDialog::setResults(const QVector<UserSearchResult>&)`
- `MainWindow::requestUserSearch(const QString&)`
- 左栏新增“搜索用户”按钮和“联系人”列表。

- [x] 新增对话框：输入框、搜索按钮、结果列表；双击结果发 `userActivated`。
- [x] MainWindow 发送 `USER_SEARCH`，解析响应并填对话框。
- [x] 双击结果：`ContactStore::add`、刷新联系人列表、关闭对话框、`selectPrivate`。
- [x] 联系人列表双击发起私聊；右键菜单删除联系人。
- [x] 登录/RESUME 后加载联系人。
- [x] 构建应用，手工验证搜索、添加、双击私聊、删除、重启后仍在。

### Task 4: 客户端日志

**Files:**
- Create: `client/core/client_log.h/.cpp`
- Modify: `client/main.cpp`
- Modify: `client/ui/main_window.cpp`
- Modify: `client/chatroom_client.pro`

**Interfaces:**
```cpp
void installClientLogging();
QString clientLogFilePath();
```
- 路径：`QStandardPaths::AppDataLocation/logs/client.log`。
- 超过 5 MB 时把现有文件改名为 `.1`。
- 安装 qInstallMessageHandler；日志包含时间、级别、消息。
- MainWindow 记录：连接成功/断开、登录/恢复成功、协议错误、发送失败、设置保存。
- 不记录密码、Token、消息正文。

- [x] 实现日志模块。
- [x] main 启动时安装；MainWindow 补关键事件日志。
- [x] 构建并手工检查日志文件存在、内容无敏感信息。

### Task 5: 设置页与 QSettings 分组

**Files:**
- Create: `client/ui/settings_dialog.h/.cpp`
- Modify: `client/ui/login_page.h/.cpp`
- Modify: `client/ui/main_window.h/.cpp`
- Modify: `client/chatroom_client.pro`

**Interfaces:**
- `SettingsDialog` 保存 host/port/tls/notifications。
- `LoginPage::setServerSettings(host, port, tls)`。
- MainWindow 读取 `settings/notifications`；关闭时隐藏到托盘可选（本期只控制通知开关）。
- `clearSession` 改为 `settings.remove("session")`，不清 `contacts/server/settings`。

- [x] LoginPage 构造时读取 `server/*`。
- [x] MainWindow 保存登录服务器到 `server/*`。
- [x] 新增设置对话框和右栏“设置”按钮。
- [x] 设置保存后更新 LoginPage、通知开关、`settings/*`。
- [x] 修改 clearSession，只删除 session 组。
- [x] 手工验证设置持久化、退出登录后联系人和设置保留。

### Task 6: 图标、版本、便携分发包

**Files:**
- Create: `client/resources/app.ico`
- Create: `client/resources/app.rc`
- Modify: `client/chatroom_client.pro`
- Modify: `client/main.cpp`
- Create: `client/package_client.ps1`
- Create: `client/installer/chatroom.iss`（模板）
- Modify: `client/README.md`, `README.md`

- [x] 生成 32x32/48x48 聊天图标。
- [x] `app.rc` 设置图标和版本 0.2.0。
- [x] `main.cpp` 设置 applicationVersion。
- [x] `package_client.ps1` 调用 build_client.ps1，复制 exe/DLL/certs/README 到 `dist/chatroom-client-0.2.0`，压缩为 ZIP。
- [x] 提供 Inno Setup `.iss` 模板（当前机器未安装，不实际编译）。
- [x] 构建并验证 ZIP 可解压运行。

### Task 7: 最终验证

- [x] 服务端 ctest 4/4。
- [x] 客户端 15+ 测试通过。
- [x] 用户搜索、联系人、设置、日志、ZIP 手工验收。
- [x] 更新审计/计划/README，记录截图和限制。

---

## 实施结果（2026-10-18）

| 检查项 | 结果 |
|---|---|
| `user_search_client.py` | 用户名/昵称搜索、空结果通过 |
| 服务端 ctest | 4/4 |
| 客户端测试 | 16 passed |
| 用户搜索 → 添加联系人 | 通过 |
| 联系人重启/退出登录保留 | 通过 |
| 设置持久化 | 通过 |
| 客户端日志 | 写入 `%APPDATA%\Chatroom\ChatroomClient\logs\client.log` |
| 图标/版本 | 0.2.0，Explorer 可读 |
| 便携 ZIP | `dist\chatroom-client-0.2.0-win64.zip`，解压可运行 |
| Inno Setup | 仅提供 `.iss` 模板，未安装工具 |

截图：`screenshot-p1-final2.png`、`screenshot-search-result.png`、`screenshot-contact-added.png`、`screenshot-settings.png`、`screenshot-settings-persist.png`。
