# 私聊与历史消息分页实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完成私聊发送/推送、离线消息可发现、最近会话列表，以及房间和私聊历史消息的游标分页。

**Architecture:** 新增 `PrivateService` 和 `HistoryService`，每个业务 worker 各持有一个实例，复用本线程 MySQL 连接。私聊先落库，再通过 `SessionManager` 查到目标在线连接，用 `IoThread::postMulticast` 推送；历史查询统一返回 `has_more + next_before_id`，客户端向上翻页。

**Tech Stack:** C++17、MySQL 8.4、现有 epoll/worker/room 框架。

**Spec:** `docs/2026-10-18-linux-chatroom-design.md`

**Status:** 已实施并验证（2026-10-18）。

## Global Constraints

- IO 线程零 SQL；私聊落库、历史查询都在业务线程。
- 私聊消息必须 `client_msg_id` 幂等，重复请求返回原 message_id，不重复推送。
- 历史每页最多 50 条，使用 `before_id` 游标，不跳页。
- 房间历史要求当前用户已加入房间；私聊历史以当前用户为其中一方。
- 消息内容 ≤ 2000 字节。

---

### Task 1: 私聊表与数据库查询

**Files:**
- Modify: `chatroom/deploy/schema.sql`
- Modify: `chatroom/server/db/mysql_connection.h/.cpp`

**Interfaces:**
- `struct HistoryMessage { uint64_t id; uint64_t sender_id; std::string nickname; std::string content; std::string created_at; };`
- `struct HistoryPage { std::vector<HistoryMessage> messages; bool has_more; uint64_t next_before_id; };`
- `struct ConversationRow { uint64_t peer_id; std::string nickname; uint64_t last_message_id; std::string last_content; std::string last_time; };`
- `bool insertPrivateMessage(...)`
- `bool listRoomHistory(int room_id, uint64_t before_id, int limit, HistoryPage&, std::string&)`
- `bool listPrivateHistory(uint64_t user_id, uint64_t peer_id, uint64_t before_id, int limit, HistoryPage&, std::string&)`
- `bool listConversations(uint64_t user_id, std::vector<ConversationRow>&, std::string&)`

- [ ] 建 `private_messages` 表与双向索引、`client_msg_id` 唯一键
- [ ] 实现私聊幂等插入
- [ ] 实现房间/私聊历史查询（limit+1 判断 has_more，反转为升序）
- [ ] 实现最近会话列表（按最后消息倒序）
- [ ] 远程执行 schema

### Task 2: 私聊服务与会话列表

**Files:**
- Create: `chatroom/server/private/private_service.h/.cpp`
- Modify: `chatroom/server/auth/session_manager.h/.cpp`
- Modify: `chatroom/server/business/worker_pool.h/.cpp`

**Interfaces:**
- `SessionManager::find(uint64_t user_id, SessionBinding&) const`
- `PrivateResult { Frame response; bool push; int target_io_thread_id; uint64_t target_connection_id; std::string push_frame; }`
- `PrivateService::handle(const Frame&, const RoomMember&)`
- 命令：`0x0030 PRIVATE_SEND`、`0x0031 PRIVATE_SEND_RESP`、`0x0032 PRIVATE_PUSH`、`0x0033 PRIVATE_CONVERSATIONS`

- [ ] 私聊校验目标用户存在、非自己、内容/client_msg_id 合法
- [ ] 落库后返回 message_id；目标在线则生成 PRIVATE_PUSH
- [ ] 重复 client_msg_id 不重复推送
- [ ] 最近会话列表带 peer、最后一条、时间、online

### Task 3: 历史消息分页

**Files:**
- Create: `chatroom/server/history/history_service.h/.cpp`
- Modify: `chatroom/common/protocol.h`
- Modify: `chatroom/server/business/worker_pool.h/.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- `HISTORY_REQ 0x0050`：`kind u8`（1房间/2私聊）、`target_id u64`、`before_id u64`、`limit u16`
- `HISTORY_RESP 0x0051`：`kind u8, target_id u64, count u16, has_more u8, next_before_id u64` + 消息记录
- 消息记录：`id u64, sender_id u64, nickname, content, created_at`

- [ ] 房间历史校验成员
- [ ] 私聊历史强制以当前 user_id 为一端
- [ ] `limit` 限制 1–50，`before_id=0` 取最新页
- [ ] `has_more`/`next_before_id` 正确

### Task 4: 联调与验证

- [ ] 两个客户端双向私聊、离线发送、上线后会话列表可见
- [ ] 私聊历史分页（limit=2，连续翻页）
- [ ] 房间历史分页与未加入房间 1007
- [ ] 重复 client_msg_id 只出现一条记录、不重复推送
- [ ] `ctest` 3/3，日志无 ERROR

---

## 实施验证结果（2026-10-18）

| 验证项 | 结果 |
|---|---|
| `ctest` | 3/3 通过 |
| 私聊发送/推送 | 双方双向发送，发送者收到 RESP，接收者收到 PUSH |
| 最近会话列表 | 包含 peer、最后一条消息、在线状态 |
| 私聊历史分页 | `limit=2` 连续翻页，`has_more/next_before_id` 正确 |
| 重复私聊 `client_msg_id` | 返回原 message_id，不重复推送 |
| 房间历史分页 | `limit=2` 连续翻页通过 |
| 未加入房间查历史 | 1007 |
| 离线私聊 | 接收方离线时落库，重新登录后历史可见 |
| 1 万连接复测 | 0 失败，0.87 秒 |
| 日志 | 无 ERROR |

当前行为：
- 私聊消息先落库，再查 `SessionManager`；目标在线才推送，离线只保留历史。
- 历史统一按 id 倒序取 `limit+1` 判断 `has_more`，返回前反转为升序。
- 私聊历史强制以当前登录用户为查询一端，客户端只能指定 peer_id。

下一步：限流、Qt 客户端、TLS/PROXY protocol。
