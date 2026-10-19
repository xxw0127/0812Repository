# 私聊未读 + 客户端提醒 + 自动登录实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 私聊会话列表显示未读数，收到非当前私聊时提醒；客户端保存 Token 并在启动时自动 RESUME，同时提供退出登录。

**Architecture:** 服务端增加 `private_read_state` 表，会话列表返回未读数，打开私聊/主动标记时更新已读位置；客户端用 `QSettings` 保存登录态，私聊推送触发任务栏/托盘提醒和未读角标。

**Spec:** `docs/2026-10-18-linux-chatroom-design.md`

**Status:** 已实施并验证（2026-10-18）。

## Global Constraints

- 未读数只统计“对方发给我的、id 大于 last_read_message_id”的私聊。
- 已读位置只增不减，使用 `GREATEST`。
- Token 存在 QSettings，不保存密码。
- RESUME 失败 1009/1010/1002 清除本地登录态。
- 不改变房间/群聊未读范围（本期只做私聊）。

---

### Task 1: 读状态表与数据库

**Files:**
- Modify: `chatroom/deploy/schema.sql`
- Modify: `chatroom/server/db/mysql_connection.h/.cpp`

- [ ] 建 `private_read_state(user_id, peer_id, last_read_message_id, updated_at)`
- [ ] `ConversationRow` 增加 `unread`
- [ ] `listConversations` 返回未读数
- [ ] `markPrivateRead(user_id, peer_id, message_id)`
- [ ] 老数据初始化为已读，避免历史消息全部变未读

### Task 2: 服务端协议

**Files:**
- Modify: `chatroom/common/protocol.h`
- Modify: `chatroom/server/private/private_service.h/.cpp`
- Modify: `chatroom/server/history/history_service.cpp`

- [ ] `PRIVATE_CONVERSATIONS_RESP` 每行增加 `unread u32`
- [ ] 新增 `PRIVATE_MARK_READ` 0x0034：peer_id u64, message_id u64
- [ ] 打开私聊最新历史时自动标记已读
- [ ] 标记已读只增不减

### Task 3: 客户端未读与提醒

**Files:**
- Modify: `chatroom/client/ui/main_window.h/.cpp`

- [ ] 会话列表显示 `(N)` 未读
- [ ] 当前私聊收到推送：标记已读、不提醒
- [ ] 非当前私聊：未读 +1、任务栏闪烁、托盘气泡
- [ ] 打开私聊后清零并刷新会话列表
- [ ] 窗口标题显示总未读

### Task 4: 自动登录与退出

- [ ] 登录/恢复成功保存 host/port/tls/token/user_id/nickname 到 QSettings
- [ ] 启动时有 Token 自动连接并 RESUME
- [ ] RESUME 失败清除登录态回登录页
- [ ] 增加“退出登录”，发送 LOGOUT 后清除本地登录态

### Task 5: 联调

- [ ] 更新 `private_history_client.py` 解析 unread
- [ ] 未读增加/清零测试
- [ ] 关闭客户端后重新打开自动登录
- [ ] 退出登录后回到登录页且本地 Token 清除
- [ ] `ctest` 3/3

---

## 实施验证结果（2026-10-18）

| 验证项 | 结果 |
|---|---|
| `private_read_state` | 表已创建，旧消息迁移为已读 |
| 未读增加 | 私聊消息后未读数 > 0 |
| 打开私聊 | 请求最新历史后未读清零 |
| 客户端未读角标 | 最近私聊显示 `(1)`，窗口标题显示 `(1)` |
| 客户端提醒 | 非当前私聊触发 `QApplication::alert` + 托盘气泡 |
| 自动登录 | 关闭客户端后重开，自动 RESUME 进入大厅 |
| 退出登录 | 回到登录页，QSettings 清除 |
| 退出后重开 | 不再自动登录，停在登录页 |
| `ctest` | 3/3 通过 |

截图：
- `client/screenshot-unread.png`
- `client/screenshot-unread-cleared.png`
- `client/screenshot-auto-login.png`
- `client/screenshot-logout.png`

已知限制：未读状态目前只做私聊；群聊房间未读未实现。托盘通知依赖 Windows 系统托盘。
