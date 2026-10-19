# 房间与群聊广播实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 TCP + MySQL 8.4 账号体系上完成房间列表、加入/退出、在线成员、群聊消息落库与房间内广播。

**Architecture:** `RoomManager` 保存房间元数据和在线成员（按 user_id 唯一），所有操作加锁；每个业务 worker 持有 `RoomService`，复用本线程 MySQL 连接写消息。群聊广播在 worker 内按 `io_thread_id` 分组，向每个 IO 线程投递一次 `postMulticast`，由 IO 线程把共享帧逐连接入发送队列。

**Tech Stack:** C++17、MySQL 8.4、现有 epoll 框架、Argon2id/Token 账号体系。

**Spec:** `docs/2026-10-18-linux-chatroom-design.md`

**Status:** 已实施并验证（2026-10-18）。

## Global Constraints

- IO 线程零 SQL；消息写库、房间成员操作全部在业务线程。
- 房间成员按 `user_id` 唯一；断线、踢下线、登出都要清理房间关系。
- 群聊消息必须 `client_msg_id` 幂等，重复请求返回原 `server_msg_id`，不重复广播。
- 单房间 1000 人上限；大厅按预置分厅。
- 消息内容 ≤ 2000 字节，`client_msg_id` ≤ 36 字节。

---

### Task 1: 房间与消息表

**Files:**
- Modify: `chatroom/deploy/schema.sql`
- Modify: `chatroom/server/db/mysql_connection.h/.cpp`
- Test: `chatroom/tests/test_auth.cpp`（保持现有测试通过）

**Interfaces:**
- `struct RoomRow { int id; std::string name; int max_members; };`
- `bool MysqlConnection::listRooms(std::vector<RoomRow>&, std::string& error);`
- `bool MysqlConnection::insertMessage(int room_id, uint64_t sender_id, const std::string& client_msg_id, const std::string& content, const std::string& created_at, uint64_t& message_id, std::string& error);`

- [ ] 建 `rooms`、`messages` 表，预置 10 个大厅 + 技术交流 + 闲聊
- [ ] 实现 `listRooms` 预处理查询
- [ ] 实现 `insertMessage`，`ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id)` 保证幂等
- [ ] 远程重新执行 schema，确认表存在

### Task 2: RoomManager 与断线清理

**Files:**
- Create: `chatroom/server/room/room_manager.h/.cpp`
- Modify: `chatroom/server/net/io_thread.h/.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- `struct RoomInfo { int id; std::string name; int max_members; };`
- `struct RoomMember { uint64_t user_id; uint64_t connection_id; int io_thread_id; std::string nickname; std::weak_ptr<Connection> connection; };`
- `RoomManager(std::vector<RoomInfo>)`
- `bool join(int room_id, const RoomMember&, std::string& error)`
- `bool leave(int room_id, uint64_t user_id)`
- `bool snapshot(int room_id, std::vector<RoomMember>&) const`
- `void removeConnection(uint64_t connection_id)`
- `IoThread` 增加断线回调 `std::function<void(uint64_t)>` 和 `postMulticast(vector<uint64_t>, string)`

- [ ] 实现房间表、成员表、connection→rooms 反向索引
- [ ] 加入/退出/成员快照/在线数
- [ ] IO 线程关闭连接时调用 RoomManager 清理
- [ ] 单房间上限 1000，重复加入幂等

### Task 3: 房间/群聊协议

**Files:**
- Modify: `chatroom/common/protocol.h`
- Create: `chatroom/server/room/room_service.h/.cpp`
- Modify: `chatroom/server/business/worker_pool.h/.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- 命令：`0x0010 ROOM_LIST`、`0x0011 JOIN_ROOM`、`0x0012 LEAVE_ROOM`、`0x0013 ROOM_MEMBERS`、`0x0020 CHAT_SEND`、`0x0021 CHAT_SEND_RESP`、`0x0022 CHAT_PUSH`
- `RoomResult { Frame response; bool broadcast; std::string broadcast_frame; std::vector<RoomMember> targets; }`
- `RoomService::handle(const Frame&, const RoomMember&)`，未认证返回 1002，不在房间返回 1007

- [ ] 房间列表返回 id/name/max/online
- [ ] 加入/退出/成员列表
- [ ] 群聊校验成员、内容长度、client_msg_id
- [ ] 消息落库后组装一次 CHAT_PUSH，按 IO 线程分组广播
- [ ] 发送者收到 CHAT_SEND_RESP；重复 client_msg_id 只返回原 id

### Task 4: 远程联调与压测

- [ ] `ctest` 3/3 通过
- [ ] Python 客户端：注册→登录→房间列表→加入→两个客户端互发群聊→成员列表→退出
- [ ] 500 人房间、50 条/秒广播冒烟，P99 延迟记录
- [ ] 断线/踢下线后房间成员数正确
- [ ] 日志无 ERROR，消息表 `client_msg_id` 唯一

---

## 实施验证结果（2026-10-18）

| 验证项 | 结果 |
|---|---|
| `ctest` | 3/3 通过 |
| 房间列表 | 12 个预置房间 |
| 加入/成员列表 | 2 个客户端加入后成员数 2 |
| 群聊广播 | 双方都收到 `CHAT_PUSH` |
| 消息落库 | `messages` 表有记录，返回 `server_msg_id` |
| 重复 `client_msg_id` | 返回原 message_id，不重复广播 |
| 退出房间后发言 | 1007 |
| 断线清理 | 成员数回到 0 |
| 踢下线清理 | 旧连接成员被移除 |
| 200 客户端广播 | 10 条消息、2000 次推送、0 失败 |
| 服务日志 | 无 ERROR |

当前单房间广播实现：worker 组装一次 `CHAT_PUSH`，按 `io_thread_id` 分组，每个 IO 线程执行一次 `postMulticast`，逐连接入队。慢客户端仍受 2 MB 写队列硬限保护。

下一步：私聊、历史消息分页、限流、Qt 客户端。
