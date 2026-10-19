# 服务端加固（限流/连接保护/systemd）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有聊天服务上补齐登录失败限流、消息令牌桶、全局/单 IP 连接上限、systemd 常驻和日志轮转，降低公网开放前的滥用与资源耗尽风险。

**Architecture:** 限流器分为共享登录限流器和每连接令牌桶；连接上限由 Acceptor 在接入时检查，IO 线程关闭连接时释放；日志通过 SIGUSR1 重新打开文件，配合 logrotate。

**Tech Stack:** C++17、epoll、systemd、logrotate。

**Spec:** `docs/2026-10-18-linux-chatroom-design.md`

**Status:** 已实施并验证（2026-10-18）。

## Global Constraints

- 登录失败限制：IP + 用户名，5 次/分钟。
- 消息限流：每连接 10 条/秒，突发 20。
- 全局连接上限默认 20,000；单 IP 默认 20，可配置为 0 关闭。
- IO 线程不做额外阻塞操作；限流判断只做内存计算。
- 日志中不出现密码、Token 原文。

---

### Task 1: 客户端 IP 与连接上限

**Files:**
- Create: `chatroom/server/net/connection_limiter.h/.cpp`
- Modify: `chatroom/server/net/connection.h/.cpp`
- Modify: `chatroom/server/net/io_thread.h/.cpp`
- Modify: `chatroom/server/net/acceptor.h/.cpp`
- Modify: `chatroom/server/config.h/.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- `Connection` 构造增加 `peer_ip`，提供 `peerIp()`。
- `ConnectionLimiter(max_connections, max_per_ip)`，`acquire(connection_id, ip, error)` / `release(connection_id)`。
- Acceptor 接入时 acquire，IO 线程关闭回调 release。

- [ ] 客户端 IP 传入 Connection
- [ ] 全局连接数上限
- [ ] 单 IP 连接数上限，0 表示关闭
- [ ] 断线、踢下线、启动失败都正确释放计数

### Task 2: 登录失败限流

**Files:**
- Create: `chatroom/server/auth/login_rate_limiter.h/.cpp`
- Modify: `chatroom/server/auth/auth_service.h/.cpp`
- Modify: `chatroom/server/auth/session_manager.h`（SessionBinding 增加 peer_ip）
- Modify: `chatroom/server/business/worker_pool.h/.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- `LoginRateLimiter::isBlocked(ip, username)` / `recordFailure` / `recordSuccess`
- 失败 5 次/60 秒返回错误码 1005
- 成功登录清除计数

- [ ] 用户名不存在、密码错误都计入失败
- [ ] 成功登录清除计数
- [ ] 不同 IP/用户名互不影响
- [ ] 错误响应 1005

### Task 3: 消息令牌桶

**Files:**
- Create: `chatroom/server/util/token_bucket.h`
- Modify: `chatroom/server/net/connection.h/.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- `TokenBucket(rate_per_second, burst)`
- `Connection::allowMessage()`
- 超限返回 1005，不投递 worker

- [ ] 群聊和私聊共用每连接令牌桶
- [ ] 10 条/秒，突发 20
- [ ] 超限只影响发送者，不断开连接

### Task 4: systemd 与日志轮转

**Files:**
- Modify: `chatroom/deploy/chatroom.service`
- Create: `chatroom/deploy/chatroom.logrotate`
- Modify: `chatroom/server/util/logging.h/.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- `reopenLogging()`
- SIGUSR1 触发重新打开日志文件
- systemd `Restart=on-failure`、`LimitNOFILE=65535`

- [ ] 安装 systemd 单元并 enable/start
- [ ] 停止 nohup 实例，避免端口冲突
- [ ] logrotate 每日/100MB，保留 7 份，postrotate 发 SIGUSR1
- [ ] 验证 `systemctl status`、`systemctl restart` 自动恢复

### Task 5: 验证

- [ ] 6 次错误密码触发 1005
- [ ] 正确密码在限流窗口后恢复
- [ ] 快速发 30 条消息，超过突发的部分返回 1005
- [ ] 单 IP 连接上限测试
- [ ] 全局连接上限配置可生效
- [ ] systemd 重启、日志轮转验证
- [ ] `ctest` 3/3

---

## 实施验证结果（2026-10-18）

| 验证项 | 结果 |
|---|---|
| 编译 / ctest | 通过，3/3 |
| 登录失败限流 | `[1004, 1004, 1004, 1004, 1004, 1005]` |
| 消息令牌桶 | 30 条突发：20 条通过，10 条返回 1005 |
| 单 IP 连接上限 | 25 个连接，20 个通过，5 个被拒绝 |
| 全局连接上限 | 配置项已生效（测试实例 0 单 IP 限制下 10,000 连接通过） |
| systemd | `chatroom.service` enabled + active |
| kill -9 自动恢复 | systemd 自动拉起，PID 变更，端口恢复 |
| `systemctl restart` | 正常停止/启动，MySQL 重连成功 |
| 日志轮转 | `chatroom.log` 轮转为 `chatroom.log.1`，新文件创建 |
| SIGUSR1 | 手动/轮转后重新打开日志成功 |
| 10,000 连接 | 测试实例 0 失败，0.90 秒 |
| 200 客户端广播 | 2,000 次推送，0 失败 |
| 服务日志 | 无 ERROR |

注意：
- 生产默认 `--max-conn-per-ip 20`；如果前面有 FRP/nginx 代理，所有客户端会呈现同一个 IP，需要把该值设为 `0` 或实现 PROXY protocol 后再启用。
- 日志目录 `logs/` 为 `xxw:xxw` 且组可写，logrotate 配置必须带 `su xxw xxw`。
- 登录限流当前为内存实现，服务重启后清零；多机部署需要换 Redis/DB 共享计数。
