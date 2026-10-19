# MySQL 自动重连与服务端监控实施计划

**Goal:** MySQL 重启后 worker 能自动恢复连接，不再永久报 2001；服务端每 10 秒输出连接、消息、错误、RSS、fd 统计，便于后续 24 小时测试。

**Architecture:** `MysqlConnection` 保存连接配置，每个数据库操作前 `mysql_ping`，失败则重连。新增全局原子计数器和统计上报线程，所有关键路径只做原子自增。

**Spec:** `docs/2026-10-18-linux-chatroom-design.md`

**Status:** 已实施并验证（2026-10-18）。

## Global Constraints

- 每个 worker 仍独占 MySQL 连接，不跨线程共享。
- 重连逻辑只在 worker 线程执行，不在 IO 线程。
- 统计输出不阻塞 IO；每 10 秒一次。
- 日志不输出密码/Token。

---

### Task 1: MySQL 自动重连

**Files:**
- Modify: `chatroom/server/db/mysql_connection.h/.cpp`
- Modify: `chatroom/server/business/worker_pool.cpp`

- [x] `MysqlConnection` 保存 `DbConfig`
- [x] 每个公开 DB 操作前 `ensureConnected`
- [x] `mysql_ping` 失败时 close + reconnect
- [x] 重连失败返回 2001，但不退出 worker
- [x] MySQL 重启后下一次请求恢复

### Task 2: 监控指标

**Files:**
- Create: `chatroom/server/util/metrics.h/.cpp`
- Modify: `chatroom/server/net/connection_limiter.cpp`
- Modify: `chatroom/server/net/connection.cpp`
- Modify: `chatroom/server/net/io_thread.cpp`
- Modify: `chatroom/server/auth/auth_service.cpp`
- Modify: `chatroom/server/db/mysql_connection.cpp`
- Modify: `chatroom/server/main.cpp`

**Interfaces:**
- `metrics()` 全局计数器
- `startStatsReporter()` / `stopStatsReporter()`
- 每 10 秒输出：
  ```text
  stats conn_total=... conn_active=... auth_ok=... auth_fail=...
        msg_in=... frames_out=... protocol_errors=... rate_limited=...
        db_errors=... rss_kb=... fds=...
  ```

- [x] 连接总数/活跃数
- [x] 登录成功/失败/限流
- [x] 消息入站/出站帧
- [x] 协议错误
- [x] DB 错误
- [x] RSS 和 fd 数
- [x] 服务退出时停止统计线程

### Task 3: 验证

- [x] 测试实例 9102 正常运行
- [x] 重启 MySQL 后登录/发消息恢复
- [x] 生产 9443 TLS 登录仍正常
- [x] 日志出现 `stats` 行且计数递增
- [x] `ctest` 4/4

---

## 实施验证结果（2026-10-18）

| 验证项 | 结果 |
|---|---|
| `mysql_ping` 自动重连 | 杀测试库连接后旧服务 2001，新服务恢复 |
| 测试实例 9102 | 登录/房间列表正常 |
| 生产 MySQL 服务重启 | 日志 `mysql connection lost` → `restored` |
| 生产 9443 TLS | MySQL 重启后登录、房间列表成功 |
| `stats` 日志 | 每 10 秒输出，计数随登录/请求递增 |
| `test_metrics` | 通过 |
| `ctest` | 4/4 通过 |

MySQL 重启后生产 stats 示例：

```text
stats conn_total=1 conn_active=0 auth_ok=1 auth_fail=0 msg_in=2
      frames_out=2 protocol_errors=0 rate_limited=0 db_errors=1
      bcast_drop=0 rss_kb=17976 fds=24
```

已知限制：`mysql_ping` 在每个业务 DB 操作前执行，会多一次数据库往返；当前规模可接受，后续可改为按时间间隔探测。MySQL 不可用期间，worker 仍会按 5 秒连接超时阻塞当前请求。
