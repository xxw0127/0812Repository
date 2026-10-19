# MySQL 8.4 账号与数据库模块实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 TCP 框架上接入 MySQL 8.4，完成注册、登录、登出、Token 恢复、单账号会话管理，并保持 IO 线程零 SQL。

**Architecture:** 新增固定数量业务工作线程，每个线程独占一个 MySQL 连接；连接按 `conn_id % worker_count` 固定路由，保证同一连接请求有序。IO 线程只负责收发与帧解析，业务线程通过 `IoThread::post` 把响应投回连接所属 IO 线程。密码使用 Argon2id，Token 随机生成且数据库只存 SHA-256 哈希。

**Tech Stack:** C++17、MySQL 8.4 C API、libargon2、OpenSSL SHA-256、现有 epoll TCP 框架、CMake/CTest。

**Spec:** `docs/2026-10-18-linux-chatroom-design.md`

**Status:** 已实施并验证（2026-10-18）。

## Global Constraints

- MySQL 8.4，`utf8mb4`，服务端预处理语句，禁止拼接 SQL。
- 每个业务线程独立 `MYSQL*`，禁止跨线程共用。
- Argon2id 参数：19 MiB、t=2、p=1、16 字节随机盐。
- Token 32 字节随机值，数据库只存 SHA-256 十六进制哈希。
- 单账号单会话：新登录踢旧连接；同 Token 重连接管旧连接；登出撤销。
- IO 线程不做数据库、Argon2 或磁盘操作。

---

### Task 1: MySQL 8.4 初始化

**Files:**
- Create: `chatroom/deploy/schema.sql`
- Create: `chatroom/conf/chatroom.env.example`
- Modify: `chatroom/tools/run_server.sh`

**Interfaces:**
- 数据库：`chatroom`
- 应用账号：`chatroom`@`localhost`、`chatroom`@`127.0.0.1`
- 密码：写入远程 `conf/chatroom.env`，权限 600，不提交到文档

- [ ] 创建 `chatroom` 数据库和业务用户
- [ ] 创建 `users`、`sessions` 表
- [ ] 用 `mysql` 客户端验证应用账号能连接并查询
- [ ] `run_server.sh` 启动前加载 `conf/chatroom.env`

### Task 2: 数据库访问与认证基础

**Files:**
- Create: `chatroom/server/db/mysql_connection.h/.cpp`
- Create: `chatroom/server/auth/password_hash.h/.cpp`
- Create: `chatroom/server/auth/token.h/.cpp`
- Test: `chatroom/tests/test_auth.cpp`

**Interfaces:**
- `class MysqlConnection { bool connect(const DbConfig&, std::string& error); bool createUser(...); bool findUser(...); bool insertSession(...); bool findSession(...); bool revokeSessions(...); bool revokeSession(...); bool updateLastLogin(...); }`
- `bool hashPassword(const std::string& password, std::string& encoded, std::string& error);`
- `bool verifyPassword(const std::string& password, const std::string& encoded);`
- `std::string generateToken();`
- `std::string hashToken(const std::string& token);`

- [ ] 写 Argon2id/Token 单测并确认失败
- [ ] 实现密码哈希、Token 生成与 SHA-256
- [ ] 实现 MySQL 连接初始化、utf8mb4、UTC 时区、预处理语句
- [ ] 单测通过

### Task 3: 业务线程池与 IO 任务投递

**Files:**
- Create: `chatroom/server/business/worker_pool.h/.cpp`
- Modify: `chatroom/server/net/io_thread.h/.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- `class WorkerPool { WorkerPool(int threads, DbConfig, SessionManager*, std::string& error); bool submit(uint64_t key, std::function<void()> task); void stop(); }`
- `IoThread::post(std::function<void()> task);`
- `IoThread::postClose(uint64_t conn_id, std::string reason);`
- `FrameHandler` 增加 `IoThread&` 参数。

- [ ] 每个 worker 拥有独立 MySQL 连接，启动失败整体失败
- [ ] 非 PING 帧投递到固定 worker
- [ ] 响应通过 `IoThread::post` 回到所属 IO 线程
- [ ] 服务 shutdown 顺序：acceptor → worker → IO

### Task 4: 注册/登录/登出/恢复

**Files:**
- Create: `chatroom/server/auth/session_manager.h/.cpp`
- Create: `chatroom/server/auth/auth_service.h/.cpp`
- Modify: `chatroom/common/protocol.h/.cpp`
- Modify: `chatroom/server/net/connection.h/.cpp`

**Interfaces:**
- 命令：`0x0001 REGISTER`、`0x0002 LOGIN`、`0x0003 LOGOUT`、`0x0004 RESUME`
- `ByteWriter::putU64` / `ByteReader::getU64`
- `Connection::setAuth(user_id, token_hash)` / `clearAuth()` / `userId()` / `tokenHash()`
- `SessionManager::bind/unbind`
- 错误码沿用设计：1001、1003、1004、1009、1010、2001

- [ ] 注册：校验参数、Argon2id、插入 users
- [ ] 登录：校验密码、生成 Token、撤销旧会话、绑定连接
- [ ] 单会话：新登录踢旧连接；RESUME 接管旧连接
- [ ] 登出：撤销 Token、清除连接认证状态
- [ ] Python 客户端集成测试全部通过

### Task 5: 远程部署与验证

- [ ] 远程 Release 构建，`ctest` 全部通过
- [ ] 重启服务，执行注册/登录/错误密码/登出/恢复测试
- [ ] 确认 `sessions` 只存哈希、日志不出现密码/Token 原文
- [ ] 记录验证结果并更新 README

---

## 实施验证结果（2026-10-18）

远程环境：Ubuntu 26.04、MySQL 8.4.11、GCC 15.2、CMake 4.2.3。

| 验证项 | 结果 |
|---|---|
| CMake 构建 | 通过，无编译错误 |
| `ctest` | 3/3 通过（protocol、buffer、auth） |
| 注册 | 成功，返回 user_id |
| 重复注册 | 1003 |
| 错误密码 | 1004 |
| 登录 | 成功，返回 64 字符 Token |
| 新登录踢旧连接 | 旧连接收到 1010 后关闭 |
| RESUME | 成功，并接管旧连接 |
| LOGOUT | 成功 |
| 登出后 RESUME | 1002 |
| 未知 Token | 1009 |
| 密码存储 | `$argon2id$v=19$m=19456,t=2,p=1`，长度 97 |
| Token 存储 | SHA-256，长度 64，不存原文 |
| 服务日志 | 无 ERROR |
| 并发登录 | 8 个连接同时登录同一用户，最终仅 1 个活跃会话 |

并发登录已用 `user_id` 分片锁串行化，锁覆盖“撤销旧会话 + 插入新会话 + SessionManager 绑定”；不同用户仍可并行。

待办：登录限流、过期会话清理任务、历史消息表。
