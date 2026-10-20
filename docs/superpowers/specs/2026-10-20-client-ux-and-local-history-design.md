# ChatBox 客户端 UX 调整与本地聊天记录设计

| 项 | 内容 |
|---|---|
| 日期 | 2026-10-20 |
| 状态 | 待评审 |
| 范围 | 仅 Windows Qt 5.15 客户端；服务端与协议零改动 |
| 分批 | 批次 A（交互调整：需求 1/2/3）；批次 B（本地历史 + 离线模式：需求 4） |

## 1. 背景与目标

ChatBox 目前的消息只存在于进程内 `MessageStore`：关闭客户端后聊天内容全部消失，历史每次都从服务端拉取；服务器不可用时客户端停留在登录页，无法查看任何内容。本轮要解决四件事：

1. 私聊不提供 @ 功能（房间保留）。
2. 会话头部改为「头像可点进资料 + 右上角 ⋮ 菜单」，消息搜索移入菜单。
3. 设置新增「开机自启动」和「系统弹窗通知」。
4. 聊天记录真正落盘（SQLite），存储位置可选、可清理，并且**断网/服务器没开时也能打开客户端翻阅本地记录**。

### 1.1 目标

- 私聊里不再出现 @ 入口、不再触发「有人@你」。
- 头部只保留 `头像 + 标题 + ⋮`；资料入口 = 点头像；消息搜索 = ⋮ 菜单第一项。
- 设置页可控开机自启动（默认静默到托盘）与系统弹窗通知。
- 本地历史：按账号隔离、房间+私聊、含引用/撤回状态、按 `message_id` 去重；目录可配置；可清理；离线只读浏览 + 本地搜索。

### 1.2 非目标

- 不做跨设备同步：本地库只在本机，换设备靠服务端历史。
- 不做离线发送 / 离线消息队列（Outbox 仍是进程内）。
- 不做导出（JSON/HTML）、不做加密存储、不做增量云备份。
- 不自动清理历史（仅手动清理；可选项「清理 N 天前」）。
- 不改服务端协议、不改数据库表结构、不改服务端行为。

## 2. 需求 1：私聊去掉 @

**现状**：输入区 `<@>` 按钮是 `buildChatPage()` 里的局部变量，房间/私聊都显示；私聊推送分支（`main_window.cpp:3177`）与房间分支（`:3110`）都会对 `content.contains("@" + nickname_)` 触发 `showMentionNotification()`。

**设计**：
- `mention_button` 提升为成员 `mention_button_`；`ContextType::Private` 时 `hide()`，房间时 `show()`（`selectRoom`/`selectPrivate` 与登录后默认状态统一处理）。
- 私聊消息处理中删除 mention 提醒分支（私聊只有未读/托盘通知，没有再一层「有人@你」）。
- 消息渲染不改（@ 只是普通文本）。
- 房间行为完全不变：@ 按钮、@ 提醒、提及菜单、`insertMention()` 均保留。

**验收**：私聊输入区看不到 @；私聊收到含 `@自己昵称` 的消息只出现普通私聊通知；房间的 @ 功能与提醒照旧。

## 3. 需求 2：头部改版与 ⋮ 菜单

**现状**：头部 = `context_avatar_`（QLabel 36px，不可点击）+ 标题 + `context_profile_button_`（"资料"，仅私聊显示）+ `load_more_button_`（"加载更早消息"）；消息搜索按钮在**输入区**（`IconButton`，`main_window.cpp:450`）。

**设计**：
- `context_avatar_` 改为可点击（新增 `ClickableLabel` 小控件或事件过滤器）：私聊 → `openProfile(current_peer_id_, current_peer_name_)`；房间 → 无动作（房间无资料页），鼠标指针保持默认。
- 删除 `context_profile_button_`（成员、创建、show/hide、连接全部移除）。
- 头部右侧新增 `QToolButton` `more_button_`，文本 `⋮`，`setPopupMode(QToolButton::InstantPopup)`，挂 `QMenu`：
  - `消息搜索`（始终可用；点击 → 现有 `openMessageSearch()`）；
  - `加载更早消息`（房间与私聊都保留；`has_more_ == true` 时启用，点击 → 现有 `onLoadMoreClicked()`）。
- 输入区移除消息搜索按钮；`mention_button_` 保留在输入区（房间可见）。
- `load_more_button_` 视觉按钮移除（只保留菜单项），`onLoadMoreClicked()` 逻辑不变。
- 菜单样式沿用现有 QSS 规则（新增 `QToolButton#MoreButton` 与 `QMenu` 样式，使用 `theme` token）。

**验收**：头部只有头像、标题、⋮；点 ⋮ → 消息搜索可用；房间菜单多一项「加载更早消息」且翻页正常；私聊点头像能进资料页；输入区不再有搜索图标。

## 4. 需求 3：设置项

**现状**：`SettingsDialog` 有服务器/端口/TLS/「私聊托盘通知」；`settingsSaved(host, port, tls, notifications)` 由 `MainWindow` 应用并写 `QSettings`；`notifications_enabled_` 同时控制托盘气泡（`tray_->showMessage`）与任务栏闪烁（`QApplication::alert`）。

**设计**：
- 设置页新增两个开关：
  1. `开机自启动`（`settings/autostart`，默认关）。勾选时写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\ChatBox = "\"<exe 绝对路径>\" --minimized"`；取消时删除该值。每次启动若已启用，用当前 exe 路径刷新该值（便携版移动目录后仍有效）。仅 win32 编译。
  2. `系统弹窗通知`（`settings/system_popup`，默认开）。关掉后：不发 `tray_->showMessage`，但保留应用内提醒（任务栏闪烁、未读角标、`notifications_enabled_` 仍作为总开关）。
- `settingsSaved` 签名扩展为 `settingsSaved(host, port, tls, notifications, system_popup, autostart)`；`MainWindow::applySettings` 落盘并即时生效。
- 启动参数 `--minimized`：`main.cpp` 解析后 `window.showMinimizedToTray()`（即不 `show()` 主窗口；托盘存在则只创建托盘图标）。托盘菜单新增：`打开 ChatBox` / `退出`（当前托盘无菜单，最小化后必须能召回）。
- 单实例保护：新增 `QLockFile`（位于 `AppData/Location/ChatBox.lock`）。第二次启动若已有实例 → 提示并退出（避免两个进程同时写本地库；开机自启 + 手动启动是最可能撞上的场景）。

**验收**：勾选开机自启动 → `reg query HKCU\...\Run` 出现带 `--minimized` 的命令 → 注销/重启后静默驻留托盘，点托盘可召回；取消后注册表值消失。关闭系统弹窗 → 私聊到达只闪烁任务栏 + 红点，不弹系统通知。

## 5. 需求 4：本地聊天记录 + 离线模式

### 5.1 架构

新增 `client/core/history_store.{h,cpp}`（QtSql / QSQLITE，无第三方库），由 `MessageStore` 以「写穿透」方式调用，`MainWindow` 只负责打开/关闭与路径配置。

```cpp
namespace chatbox { namespace client {

class HistoryStore {
public:
    bool open(const QString& directory, quint64 user_id, QString* error);  // 建库/建表/迁移
    void close();
    bool isOpen() const;
    QString directory() const;
    qint64 sizeBytes() const;

    // 写入（幂等：同一条服务端消息重复写入只更新）。只接受 message_id > 0 的消息；
    // 本地待发/失败消息仍只存在内存 Outbox 中，不落盘。
    bool saveMessage(const ChatMessage& message);
    bool markRecalled(quint64 message_id);
    bool saveConversation(const ConversationRow& row);   // 离线侧栏用

    // 读取
    QVector<ChatMessage> recent(ChatContext context, quint64 target_id, int limit) const;
    QVector<ChatMessage> before(ChatContext context, quint64 target_id,
                                quint64 before_id, int limit) const;
    QVector<ChatMessage> search(const QString& keyword, int limit) const;  // 当前用户全部会话
    QVector<ConversationRow> conversations() const;

    // 清理
    bool clearAll();
    bool clearOlderThan(int days);

private:
    QString directory_;
    quint64 user_id_ = 0;
    QString connection_name_;   // 每账号一个连接名，避免与测试/多账号冲突
};

}}  // namespace
```

- 存储引擎：SQLite（`QSQLITE`），`PRAGMA journal_mode=WAL`、`synchronous=NORMAL`、`user_version=1`（后续迁移用）。
- 写入线程：UI 线程同步写（单条 INSERT/UPDATE，数据量小）；不引入额外线程，避免跨线程 QSqlDatabase 约束。
- 每账号独立连接名（`history_<user_id>`），同一进程多账号切换时关闭旧连接。

### 5.2 表结构（v1）

```sql
CREATE TABLE IF NOT EXISTS messages (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id           INTEGER NOT NULL,          -- 归属账号（本机隔离）
    context           INTEGER NOT NULL,          -- 0 房间 / 1 私聊
    target_id         INTEGER NOT NULL,          -- room_id 或 peer_id
    message_id        INTEGER NOT NULL,          -- 服务端消息 id（只存已确认消息）
    sender_id         INTEGER NOT NULL,
    sender_name       TEXT    NOT NULL DEFAULT '',
    content           TEXT    NOT NULL,
    created_at_utc    TEXT    NOT NULL,          -- ISO8601 UTC
    reply_to_id       INTEGER NOT NULL DEFAULT 0,
    reply_sender_name TEXT    NOT NULL DEFAULT '',
    reply_content     TEXT    NOT NULL DEFAULT '',
    recalled          INTEGER NOT NULL DEFAULT 0,
    outgoing          INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX IF NOT EXISTS uk_msg_server
    ON messages(user_id, context, target_id, message_id);
CREATE INDEX IF NOT EXISTS idx_msg_page
    ON messages(user_id, context, target_id, message_id);

CREATE TABLE IF NOT EXISTS conversations (
    user_id        INTEGER NOT NULL,
    context        INTEGER NOT NULL,
    target_id      INTEGER NOT NULL,
    title          TEXT    NOT NULL,             -- 房间名/昵称/备注
    last_message_id INTEGER NOT NULL DEFAULT 0,
    last_content   TEXT    NOT NULL DEFAULT '',
    last_time_utc  TEXT    NOT NULL DEFAULT '',
    PRIMARY KEY (user_id, context, target_id)
);
```

- 不存未读数（在线时以服务端为准；离线浏览不需要未读）。
- 不存头像（离线头像走字母回退，已知限制）。

### 5.3 写入时机

- `MessageStore::replaceHistory()` / `prependHistory()`：整批 upsert（服务端历史，message_id 均 > 0）。
- `appendIncoming()`：单条 upsert（收到的消息已带服务端 id）。
- `markSent()`：消息拿到服务端 id 后插入（`client_msg_id` 只用于内存匹配，不入库）。
- `markRecalled()`：更新对应行的 `recalled`。
- **不落盘**：`addPending()` / `markSending()` / `markFailed()` / `markAllSendingFailed()` —— 未确认消息仍只存在内存 Outbox，重启即丢弃；服务端若已收下，下次历史拉取会补回来（这是“离线发送不属于本轮范围”的自然结果）。
- 每次写入后更新 `conversations` 的 `last_*` 与 `title`（房间用房间名，私聊用备注优先、否则昵称）。
- `MessageStore` 增加可选 `HistoryStore*` 依赖；未注入时行为与现在完全一致（测试可只测内存逻辑）。

### 5.4 读取时机与显示策略

- 打开会话（`selectRoom`/`selectPrivate`）：
  1. 清空视图 → 立即从本地库取最近 50 条渲染（在线/离线都一样，消除“空白一闪”）；
  2. 在线：照旧发 `HISTORY`，响应到达后 `replaceHistory()` 覆盖为服务端最新页（本地已写入，不丢数据）；
  3. 离线：不发请求；`has_more_` 由本地 `before()` 是否还有数据决定，菜单「加载更早消息」走本地分页。
- 新消息到达/发送成功：先更新内存，再写库（写库失败只记日志，不打断 UI）。
- 离线会话列表：左侧「房间」「私聊」用 `conversations()` 结果填充（离线时房间名来自历史记录；从未聊过的房间不显示）。
- 离线搜索：⋮ → 消息搜索 → 在本地库按 `content LIKE %keyword%` 搜索**当前会话**（与服务端搜索语义一致，`MessageSearchDialog` 与结果结构不用改），状态栏标注“本地记录”；在线时保持现有服务端搜索行为。跨会话全库搜索留作后续增强。

### 5.5 存储位置与清理（设置页）

- 路径配置 `settings/history/path`，默认 `QStandardPaths::AppDataLocation + "/history"`。
- 设置页显示：当前目录（可选文本）、当前库大小、按钮【更改目录…】【打开目录】【清理…】。
- 更改目录：
  - 选择文件夹 → 校验可写（`QFileInfo::isWritable` + 实际建临时文件）；
  - 询问「是否把现有记录迁移到新位置？」→ 是：关闭连接、复制原库文件（含 `-wal`/`-shm`）到新目录、重新打开并校验行数；否：在新目录建新库（旧目录保留，提示可手动删除）。
- 清理…：对话框两项 —— 「清空全部本地记录」（二次确认，输入“清空”或勾选确认）与「清理 30 天前的记录」（天数可调 1–365）。
- 清理语义（明确写进 UI 文案）：**只删本机副本，不删服务器历史**。
  - 在线：清理后当前视图保持不动；重新打开会话或收到新消息时会从服务端重新拉取并再次落盘。也就是说“清理”不能当“永久删除聊天记录”用，永久删除需服务端功能（本轮不做）。
  - 离线：清理后当前会话立即清空，侧栏按剩余消息重算；没有剩余消息的会话从离线侧栏移除。
  - `clearOlderThan(days)` 同理：删除过期行后重算 `conversations.last_*`，空会话行一并删除。

### 5.6 离线模式（状态机）

```
启动 ──有保存会话(token)──► 连接中 ──成功──► 在线（现有行为）
   │                          │
   │                          └─失败/超时─┬─ 有该 user_id 的本地库 ──► 离线模式
   │                                       └─ 无 ──► 登录页（显示连接失败）
   └─无会话──► 登录页（新增「查看本地记录」入口：用上次 user_id 打开只读离线模式）
```

- 离线模式 UI：
  - 页面顶部横幅：`离线模式 · 仅显示本地记录`，右侧按钮【重试连接】；
  - 输入区禁用（沿用 `online_ == false` 的现有禁用逻辑）；@ 按钮、成员列表、好友申请、用户搜索、资料页、消息撤回/重发等服务端操作全部禁用或隐藏；
  - 左栏只显示本地有记录的会话；底部自己的资料栏用本地缓存的昵称/ID（无头像则字母回退）；
  - 连接成功后自动退出离线模式：执行 RESUME → 刷新房间/好友/会话列表（现有流程），横幅消失。
- 自动重连定时器在离线模式下继续生效（沿用现有指数退避），成功后无缝切回在线。
- 退出登录：保留本地库（按 `user_id` 隔离），登录页仍可「查看本地记录」。

### 5.7 与现有会话语义的关系

- `session/user_id` 已持久化，离线模式用它选库；`session/nickname` 用于显示。
- 本地库不参与鉴权：离线模式永远只读、不发送任何帧。
- 服务端仍是唯一权威：在线时用户看到的仍是服务端历史与状态，本地库只是副本。

## 6. 构建与打包

- `chatbox.pro` / `tests.pro`：`QT += sql`；新增 `core/history_store.{h,cpp}`。
- `build_client.ps1`：复制 `D:\Qt\5.15.2\mingw81_64\plugins\sqldrivers\qsqlite.dll` 到 `release\sqldrivers\`（与 `platforms` 同级）；缺少时警告（本地历史不可用，其余功能不受影响）。
- `package_client.ps1`：把 `sqldrivers` 一并打进 ZIP。**同时修复现有缺陷**：该脚本只复制 `certs/platforms/styles`，漏了 `imageformats`，导致便携包里 JPEG 头像加载失败；这次改成按目录列表统一复制（`certs, platforms, styles, imageformats, sqldrivers`）。
- 新增单实例锁文件不计入仓库（运行时文件）。

## 7. 测试计划

**单元测试（QtTest + 临时目录，`QT += sql`）**
- `historyStoreCreatesSchema`：打开新目录 → 表/索引存在 → 重复打开幂等。
- `historyStoreSaveAndLoadRecent`：upsert 若干消息 → `recent()` 顺序正确 → `before()` 分页正确。
- `historyStoreDedupesByMessageId`：同一条服务端消息重复写入只更新、不产生第二行。
- `historyStoreSkipsUnconfirmedMessages`：`message_id == 0` 的消息不落盘。
- `historyStoreMarksRecalled`：撤回状态落盘并读回。
- `historyStoreIsolatesUsers` / `historyStoreIsolatesConversations`。
- `historyStoreSearchFindsInConversation`（跨会话不串）。
- `historyStoreClearAllAndOlderThan`（含空会话清理与 `last_*` 重算）。
- `historyStorePathMoveCopiesData`（切目录迁移）。
- `messageStoreWritesThroughWhenHistoryAttached`（注入 HistoryStore 后确认消息落盘、pending/failed 不落盘；不注入时行为不变）。
- 源守卫：`privateChatHidesMentionEntry`、`headerUsesAvatarAndMoreMenu`（无 `context_profile_button_` 残留、⋮ 含消息搜索、输入区无搜索按钮）、`buildScriptShipsSqlDriver`。

**手动验收**
- 停掉服务端（`systemctl stop chatbox`）→ 重启客户端 → 进入离线模式 → 能切换会话、翻页、搜索本地记录；启动服务端 → 自动恢复在线。
- 换目录（迁移/不迁移）→ 关闭重开 → 数据在新目录；清理全部 → 会话列表清空、当前视图清空。
- 开机自启动：注册表值正确、重启后静默托盘、托盘菜单可打开主窗口；取消后值删除。
- 通知：关系统弹窗后只有任务栏闪烁 + 红点。

## 8. 风险与限制

| 风险 | 处理 |
|---|---|
| SQLite 插件缺失（便携包漏拷） | 构建脚本 + 打包脚本强制复制，缺失时警告；历史功能降级但应用可用 |
| 两实例同时写库 | 单实例锁（QLockFile） |
| 大历史库导致启动慢 / 卡顿 | 每会话只渲染最近 50 条；分页读取；清理功能；后续可加自动裁剪 |
| UI 线程同步写库 | 单条 upsert 量级小；若实测卡顿再迁到独立线程（预留 `HistoryStore` 接口不变） |
| 离线头像缺失 | 已知限制，字母头像回退；不做本地头像缓存（可后续） |
| 切目录/清理误操作 | 二次确认；迁移先校验目标可写；清理只影响本地库，不影响服务端历史 |

## 9. 实施顺序建议

1. **批次 A**（需求 1/2/3）：交互调整 + 设置项 + 单实例/托盘菜单；小步提交，可先交付使用。
2. **批次 B1**：`HistoryStore` + `MessageStore` 写穿透 + 单元测试（不影响现有 UI 行为）。
3. **批次 B2**：打开会话先读本地、在线覆盖、离线模式状态机、离线搜索、设置页路径/清理、打包复制 `sqldrivers`。
4. 全程 `run_client_tests.ps1` 回归；批次 B 完成后做一次服务端停机演练。

## 10. 我替你定的默认值（有异议就改）

| 项 | 默认 | 理由 |
|---|---|---|
| 开机自启动后的窗口状态 | 静默驻留托盘（`--minimized`），点托盘图标打开主窗口 | 聊天软件开机弹窗打扰；托盘菜单补「打开 ChatBox / 退出」 |
| 系统弹窗通知默认值 | 开 | 与现状一致（现在默认 `settings/notifications = true`） |
| 两个通知开关的关系 | `消息提醒` 是总开关；`系统弹窗通知` 只在总开关打开时生效 | 关总开关 → 无闪烁、无红点、无弹窗 |
| 清理功能的语义 | 只删本机副本；在线重新打开会话会从服务器再拉一遍并再次落盘 | 服务端是权威；真正的“永久删除”需要服务端功能，本轮不做 |
| 本地库保存范围 | 保存全部已确认消息（房间+私聊），不自动裁剪 | 满足“断网翻记录”；磁盘占用靠手动清理控制 |
| 待发/失败消息 | 不落盘（重启即丢） | 与“不做离线发送”一致；服务端已收下的消息下次拉取会补回 |
| 离线搜索范围 | 当前会话（与在线搜索一致） | 复用现有搜索对话框与结果结构，不扩大改动面 |
| 切换存储目录 | 弹窗询问「迁移已有记录？」；选“否”则旧库留在原目录并提示可手动删除 | 防止误以为数据丢了 |
| 头像 | 离线不缓存，字母头像回退 | 控制本轮范围；后续可加本地头像缓存 |
| 多账号 | 本地库按 `user_id` 隔离；登录页「查看本地记录」打开上次账号 | 同一台机器换账号不串数据 |

## 11. 评审记录

- 2026-10-20 独立 reviewer（builder-check）：提出 3 个可能影响计划的问题（待发消息是否落盘、离线搜索结果是否可跳转/带会话上下文、清理对在线视图的影响）。均已在本版定案：只存已确认消息；离线搜索限定当前会话；清理只清本机副本。评审未发现必须由需求方回答的阻塞问题。

