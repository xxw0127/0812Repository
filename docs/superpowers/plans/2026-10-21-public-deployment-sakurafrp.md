# ChatBox 公网部署与限流实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 通过 SakuraFrp 免费 TCP 隧道把 ChatBox 暴露到 `frp-oil.com:15318`，同时保留局域网 `192.168.2.240:9443`，并加入真实 IP 限流、TLS、备份和服务开机自启；不改 UFW、合盖睡眠或 Wi-Fi 省电。

**Architecture:** 公网客户端连 SakuraFrp 节点，节点把带 PROXY v2 头的 TCP 流转给服务器本机 `127.0.0.1:9444` 的 nginx；nginx 终止 TLS 后转发到 `127.0.0.1:9100` 的 chatbox。局域网仍走 `9443`。服务端在解析 PROXY v2 后按真实 IP 做连接限流，注册限流在 AuthService 内按 IP + 全局窗口计数。

**Tech Stack:** C++17、Linux epoll、nginx 1.28 stream、MySQL 8.4、systemd、UFW、OpenSSL、SakuraFrp 分发版 frpc、Qt 5.15 客户端。

**Spec:** `docs/superpowers/specs/2026-10-21-public-deployment-sakurafrp-design.md`

## Global Constraints

- 服务端代码必须保持 C++17、`-Wall -Wextra`、4 空格缩进、K&R 大括号；不引入 C++20。
- 网络帧字段保持大端；禁止直接发送结构体。
- 客户端不得直连 MySQL；本次不修改客户端协议和 TLS 校验逻辑。
- 公网端点固定为 `frp-oil.com:15318`；隧道本地目标必须是 `127.0.0.1:9444`，自定义设置必须是 `proxy_protocol_version = v2`。
- 限流值固定：注册每 IP 20 次/小时、全局 100 次/小时；连接全局 500、每 IP 50。
- 保留 `/etc/chatbox/certs/ca.crt` 和 `ca.key`；只重签服务端证书，SAN 必须包含 `IP:192.168.2.240`、`DNS:chatbox.local`、`DNS:frp-oil.com`。
- token、隧道 ID、数据库密码、CA 私钥不得进入聊天、仓库、日志或 shell 历史。
- 所有远程操作只经 SSH/scp；不使用 FTP/SMB/共享目录。
- 服务器源码路径为 `/home/xxw/桌面/0917/chatbox`；Windows 仓库路径为 `D:\OpenCode_WorkSpace`。
- 线上服务在测试通过前不重启；构建测试使用独立 `build-test/`，不覆盖正在运行的 `build/chatbox_server`。
- 免费额度约 5 GiB/月、10 Mbps；超额和宿舍断电/断网是已知外部故障，不伪装成高可用。
- 用户上课也要使用同一台笔记本：**不启用/修改 UFW，不改 logind 合盖策略，不改 NetworkManager/Wi-Fi 省电**；只启用服务开机自启。合盖/睡眠时公网断开是明确接受的行为。

## Review Focus

1. PROXY v2 头被拆成多次 `recv` 时，连接不能提前按 `127.0.0.1` 计数，也不能在头不完整时 rekey；测试必须覆盖分片头（Task 2）。
2. PROXY v2 `LOCAL` 命令没有真实地址时，连接不能被计入某个伪造 IP；测试必须覆盖 LOCAL（Task 2）。
3. 注册窗口边界：第 21 次同 IP 请求必须被拒，第 101 次全局请求必须被拒，窗口过期后恢复；测试必须覆盖（Task 1）。
4. 连接释放：被 rekey 的连接关闭后必须释放真实 IP 配额，10 条/50 条上限不能在重连后泄漏；测试必须覆盖（Task 2）。
5. nginx `9444` 必须只绑定 loopback，`9443` 必须仍对局域网可用；证书 SAN 缺 `frp-oil.com` 时客户端会拒绝；用 `ss`/`openssl` 验收（Task 4、Task 9）。

---

## 执行前的共同约定

- 从 `main` 创建分支 `feat/public-deployment`，从设计提交 `ff639a3` 开始。
- C++ 代码在 Windows 编辑；测试和构建在服务器执行。每个任务的“远程测试”约定如下：
  - 用 `scp` 把本任务改动的文件复制到 `/home/xxw/桌面/0917/chatbox` 对应路径。
  - 在服务器用 `cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release` 配置，`cmake --build build-test -j4` 构建，`ctest --test-dir build-test --output-on-failure` 运行。
  - 运行中的 `build/chatbox_server` 不被覆盖；只有 Task 3 在全部测试通过后才重建并重启。
- 任何 `scp`/`ssh` 命令失败时先停，不要用 `--force` 覆盖未知文件；服务器源码有差异时先 `diff` 再决定。
- SakuraFrp 的用户操作（面板改端口、放 token）不写进仓库；token 文件只在服务器 root 600 存在。
- 在 Windows PowerShell 中，含 `$(...)` 的远程命令要用单引号包住整个 `ssh` 参数，或先登录服务器再执行；不要让 PowerShell 在本地展开远程 `date`/`latest`。

---

### Task 1: 注册限流器

**Files:**
- Create: `chatbox/server/auth/registration_rate_limiter.h`
- Create: `chatbox/server/auth/registration_rate_limiter.cpp`
- Create: `chatbox/tests/test_registration_rate_limiter.cpp`
- Modify: `chatbox/server/auth/auth_service.h`
- Modify: `chatbox/server/auth/auth_service.cpp`
- Modify: `chatbox/server/business/worker_pool.h`
- Modify: `chatbox/server/business/worker_pool.cpp`
- Modify: `chatbox/CMakeLists.txt`

**Interfaces:**
- Produces: `RegistrationRateLimiter(int max_per_ip, int max_global, int window_seconds)`
- Produces: `bool RegistrationRateLimiter::allow(const std::string& ip, std::chrono::steady_clock::time_point now)`
- Produces: `AuthService(MysqlConnection&, SessionManager&, UserLockTable&, LoginRateLimiter&, RegistrationRateLimiter&)`
- Produces: `AuthResult AuthService::handleRegister(const Frame&, const SessionBinding&)`
- Consumes: `SessionBinding::peer_ip` from `worker_pool.cpp`.

**Steps:**

- [ ] **Step 1: 写失败测试**

```cpp
#include <chrono>
#include <string>

#include "server/auth/registration_rate_limiter.h"
#include "tests/test_framework.h"

using Clock = std::chrono::steady_clock;

int testPerIpAndWindow() {
    chatbox::RegistrationRateLimiter limiter(20, 100, 3600);
    const auto t0 = Clock::time_point{};
    for (int i = 0; i < 20; ++i) {
        CHECK_TRUE(limiter.allow("203.0.113.7", t0));
    }
    CHECK_TRUE(!limiter.allow("203.0.113.7", t0));
    CHECK_TRUE(limiter.allow("203.0.113.8", t0));
    CHECK_TRUE(limiter.allow("203.0.113.7", t0 + std::chrono::seconds(3601)));
    return 0;
}

int testGlobalLimit() {
    chatbox::RegistrationRateLimiter limiter(20, 100, 3600);
    const auto t0 = Clock::time_point{};
    for (int i = 0; i < 100; ++i) {
        CHECK_TRUE(limiter.allow("198.51.100." + std::to_string(i + 1), t0));
    }
    CHECK_TRUE(!limiter.allow("198.51.100.200", t0));
    CHECK_TRUE(limiter.allow("198.51.100.200", t0 + std::chrono::seconds(3601)));
    return 0;
}

int main() {
    int failures = 0;
    failures += testPerIpAndWindow();
    failures += testGlobalLimit();
    if (failures == 0) {
        std::cout << "test_registration_rate_limiter: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 在服务器确认测试先失败**

先把测试文件复制到服务器，并加入测试目标的 CMake 配置（`server/auth/registration_rate_limiter.cpp` 可以先不存在），然后执行：

```bash
ssh chatroom-dev "cd '/home/xxw/桌面/0917/chatbox' && cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release && cmake --build build-test -j4"
```

Expected: CMake 配置或编译失败，提示找不到 `server/auth/registration_rate_limiter.h` / 源文件。若构建目录尚未创建，`cmake` 先成功是正常的；失败必须出现。

- [ ] **Step 3: 实现 `RegistrationRateLimiter`**

头文件公开精确签名：

```cpp
class RegistrationRateLimiter {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    RegistrationRateLimiter(int max_per_ip, int max_global, int window_seconds);
    bool allow(const std::string& ip, TimePoint now);

private:
    struct Entry {
        int count = 0;
        TimePoint window_start;
    };

    int max_per_ip_;
    int max_global_;
    std::chrono::seconds window_;
    mutable std::mutex mutex_;
    Entry global_;
    std::unordered_map<std::string, Entry> per_ip_;
};
```

`allow` 在锁内按以下顺序执行：清理全局窗口、检查全局上限、清理该 IP 窗口、检查 IP 上限、分别递增两个计数。窗口从该条目首次请求开始；`now - window_start > window_` 时重置。`per_ip_.size() > 4096` 时删除过期条目。被拒绝时不递增任何计数。

- [ ] **Step 4: 接入 AuthService**

`AuthService` 构造函数增加 `RegistrationRateLimiter&` 并保存引用。`handle` 的 `kCmdRegister` 分支改为 `return handleRegister(request, binding);`。`handleRegister` 在用户名、密码、昵称校验通过后、`hashPassword` 之前执行：

```cpp
if (!registration_limiter_.allow(binding.peer_ip,
                                 std::chrono::steady_clock::now())) {
    metrics().rate_limited.fetch_add(1, std::memory_order_relaxed);
    return AuthResult{makeError(request.header.seq, 1005,
                                "too many registrations")};
}
```

错误码固定 `1005`，消息固定 `too many registrations`。重复用户名、数据库错误也保留已消耗的配额。

- [ ] **Step 5: 在 WorkerPool 共享实例**

`worker_pool.h` 增加 `#include "server/auth/registration_rate_limiter.h"` 和成员：

```cpp
RegistrationRateLimiter registration_limiter_{20, 100, 3600};
```

`workerLoop` 构造 `AuthService` 时追加 `registration_limiter_`。这样所有 worker 共享同一份 IP/全局计数。

- [ ] **Step 6: 确认 CMake 接入**

在 `chatbox_server` 源列表加入 `server/auth/registration_rate_limiter.cpp`；测试目标若在 Step 2 已添加，不要重复添加。测试目标为：

```cmake
add_executable(test_registration_rate_limiter
    tests/test_registration_rate_limiter.cpp
    server/auth/registration_rate_limiter.cpp)
target_include_directories(test_registration_rate_limiter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_registration_rate_limiter PRIVATE Threads::Threads)
add_test(NAME registration_rate_limiter COMMAND test_registration_rate_limiter)
```

- [ ] **Step 7: 远程运行测试**

```bash
ssh chatroom-dev "cd '/home/xxw/桌面/0917/chatbox' && cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release && cmake --build build-test -j4 && ctest --test-dir build-test --output-on-failure"
```

Expected: `registration_rate_limiter` 通过；整个 `ctest` 无失败。

- [ ] **Step 8: 提交**

```bash
git add chatbox/server/auth/registration_rate_limiter.h \
  chatbox/server/auth/registration_rate_limiter.cpp \
  chatbox/server/auth/auth_service.h chatbox/server/auth/auth_service.cpp \
  chatbox/server/business/worker_pool.h chatbox/server/business/worker_pool.cpp \
  chatbox/tests/test_registration_rate_limiter.cpp chatbox/CMakeLists.txt
git commit -m "feat(auth): add per-IP and global registration rate limiting"
```

---

### Task 2: PROXY 真实 IP 后的连接限流

**Files:**
- Modify: `chatbox/server/net/connection_limiter.h`
- Modify: `chatbox/server/net/connection_limiter.cpp`
- Modify: `chatbox/server/net/acceptor.h`
- Modify: `chatbox/server/net/acceptor.cpp`
- Modify: `chatbox/server/net/io_thread.h`
- Modify: `chatbox/server/net/io_thread.cpp`
- Modify: `chatbox/server/main.cpp`
- Create: `chatbox/tests/test_connection_limiter.cpp`
- Create: `chatbox/tests/test_connection_proxy.cpp`
- Modify: `chatbox/CMakeLists.txt`

**Interfaces:**
- Produces: `bool ConnectionLimiter::acquire(uint64_t id, const std::string& ip, std::string& error, bool defer_per_ip = false)`
- Produces: `bool ConnectionLimiter::rekey(uint64_t id, const std::string& ip, std::string& error)`
- Produces: `Acceptor(int port, std::vector<IoThread*> io_threads, ConnectionLimiter& limiter, std::string listen_address, bool proxy_protocol)`
- Produces: `IoThread(int id, FrameHandler handler, int idle_timeout_sec, CloseHandler close_handler, bool proxy_protocol, ProxyRekeyHandler proxy_rekey_handler)`
- Consumes: `Connection::peerIp()` after `tryParseProxyHeader`.

**Steps:**

- [ ] **Step 1: 写失败测试**

`tests/test_connection_limiter.cpp`：

```cpp
#include <string>

#include "server/net/connection_limiter.h"
#include "tests/test_framework.h"

int testPerIpRelease() {
    chatbox::ConnectionLimiter limiter(2, 1);
    std::string error;
    CHECK_TRUE(limiter.acquire(1, "a", error));
    CHECK_TRUE(!limiter.acquire(2, "a", error));
    CHECK_TRUE(limiter.acquire(3, "b", error));
    limiter.release(1);
    CHECK_TRUE(limiter.acquire(4, "a", error));
    limiter.release(2);
    limiter.release(3);
    limiter.release(4);
    return 0;
}

int testDeferredRekey() {
    chatbox::ConnectionLimiter limiter(10, 1);
    std::string error;
    CHECK_TRUE(limiter.acquire(1, "127.0.0.1", error, true));
    CHECK_TRUE(limiter.acquire(2, "127.0.0.1", error, true));
    CHECK_TRUE(limiter.rekey(1, "203.0.113.7", error));
    CHECK_TRUE(!limiter.rekey(2, "203.0.113.7", error));
    limiter.release(1);
    CHECK_TRUE(limiter.rekey(2, "203.0.113.7", error));
    limiter.release(2);
    return 0;
}

int testFiftyPerIp() {
    chatbox::ConnectionLimiter limiter(100, 50);
    std::string error;
    for (int i = 1; i <= 50; ++i) {
        CHECK_TRUE(limiter.acquire(static_cast<uint64_t>(i), "192.0.2.50", error));
    }
    CHECK_TRUE(!limiter.acquire(51, "192.0.2.50", error));
    limiter.release(1);
    CHECK_TRUE(limiter.acquire(52, "192.0.2.50", error));
    for (int i = 2; i <= 50; ++i) {
        limiter.release(static_cast<uint64_t>(i));
    }
    limiter.release(52);
    return 0;
}

int main() {
    int failures = 0;
    failures += testPerIpRelease();
    failures += testDeferredRekey();
    failures += testFiftyPerIp();
    if (failures == 0) {
        std::cout << "test_connection_limiter: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
```

`tests/test_connection_proxy.cpp` 用 `socketpair(AF_UNIX, SOCK_STREAM, 0, fds)` 创建 `Connection(fds[0], 1, 0, "127.0.0.1", true)`，构造 PROXY v2 TCP4 头（signature、`0x21`、`0x11`、长度 12、源 `203.0.113.7`、目的 `192.0.2.1`、源/目的端口），断言：

```cpp
connection->input().append(reinterpret_cast<const char*>(header), 10);
CHECK_TRUE(connection->tryParseProxyHeader(error));
CHECK_TRUE(connection->proxyHeaderPending());
CHECK_EQ(connection->peerIp(), std::string("127.0.0.1"));

connection->input().append(reinterpret_cast<const char*>(header + 10), total - 10);
CHECK_TRUE(connection->tryParseProxyHeader(error));
CHECK_TRUE(!connection->proxyHeaderPending());
CHECK_EQ(connection->peerIp(), std::string("203.0.113.7"));
```

再测 `LOCAL`（`0x20`、长度 0）保持初始 IP、非法文本头返回 false。测试结束 `close(fds[1])`；`Connection` 析构会关闭 `fds[0]`。

- [ ] **Step 2: 确认测试先失败**

复制测试并加入两个测试目标的 CMake 配置后运行：

```bash
ssh chatroom-dev "cd '/home/xxw/桌面/0917/chatbox' && cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release && cmake --build build-test -j4"
```

Expected: 编译失败，因为 `rekey`/`defer_per_ip` 不存在。

- [ ] **Step 3: 修改 ConnectionLimiter**

把 `connection_ips_` 从 `unordered_map<uint64_t, string>` 改为记录：

```cpp
struct TrackedConnection {
    std::string ip;
    bool counted = false;
};
std::unordered_map<uint64_t, TrackedConnection> connections_;
```

`acquire` 保留原有全局计数；只有 `!defer_per_ip && max_per_ip_ > 0` 时检查并递增 `ip_counts_`，并设置 `counted=true`。`defer_per_ip=true` 时只登记连接，不占 IP 配额。

`rekey` 在锁内执行：

- 找不到连接：`error = "connection not tracked"`，返回 false。
- 已经 `counted`：同 IP 返回 true，不同 IP 返回 false（该连接只允许 rekey 一次）。
- `max_per_ip_ > 0` 且新 IP 已达到上限：`error = "per-ip connection limit reached"`，返回 false，不递增。
- 否则递增新 IP、更新 `ip`、置 `counted=true`，返回 true。
- `max_per_ip_ == 0` 时只更新 `ip`、置 `counted=false`，返回 true。

`release` 只在 `counted=true` 时递减 IP 计数；无论是否 counted 都递减全局 active 并删除记录。

- [ ] **Step 4: Acceptor 延迟计数**

构造函数追加 `bool proxy_protocol` 成员。`accept` 时调用：

```cpp
limiter_.acquire(connection_id, client_ip, limit_error, proxy_protocol_)
```

当连接来自 nginx 且 `proxy_protocol_` 为 true 时，accept 阶段只占全局名额；真实 IP 由 Task 5 的 IoThread 回调 rekey。

- [ ] **Step 5: IoThread rekey 回调**

在 `io_thread.h` 增加：

```cpp
using ProxyRekeyHandler =
    std::function<bool(uint64_t, const std::string&, std::string&)>;
```

构造函数参数末尾增加 `ProxyRekeyHandler proxy_rekey_handler = nullptr`，保存成员。`handleRead` 中记录 `had_pending = connection->proxyHeaderPending()`；在 `tryParseProxyHeader` 返回 true 且 `had_pending && !connection->proxyHeaderPending()` 时调用：

```cpp
std::string rekey_error;
if (proxy_rekey_handler_ &&
    !proxy_rekey_handler_(connection->id(), connection->peerIp(), rekey_error)) {
    logMessage(LogLevel::kWarn, "io",
               "conn=" + std::to_string(connection->id()) +
                   " rekey rejected: " + rekey_error);
    closeConnection(connection, rekey_error);
    return;
}
```

仅真实解析完成时调用一次；`LOCAL` 头也会完成解析，但 `peerIp()` 保持初始代理地址，不按伪造地址计数。

- [ ] **Step 6: main 接线**

`IoThread` 构造时传入：

```cpp
[&connection_limiter](uint64_t id, const std::string& ip, std::string& error) {
    return connection_limiter.rekey(id, ip, error);
}
```

`Acceptor` 构造时追加 `config.proxy_protocol`。关闭回调里已有的 `connection_limiter.release(connection_id)` 保持不变，确保 rekey 失败和正常关闭都释放。

- [ ] **Step 7: CMake 与测试**

测试目标若在 Step 2 已添加，不要重复添加。目标为：

```cmake
add_executable(test_connection_limiter tests/test_connection_limiter.cpp)
target_link_libraries(test_connection_limiter PRIVATE chatbox_net)
add_test(NAME connection_limiter COMMAND test_connection_limiter)

add_executable(test_connection_proxy tests/test_connection_proxy.cpp)
target_link_libraries(test_connection_proxy PRIVATE chatbox_net)
add_test(NAME connection_proxy COMMAND test_connection_proxy)
```

远程运行：

```bash
ssh chatroom-dev "cd '/home/xxw/桌面/0917/chatbox' && cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release && cmake --build build-test -j4 && ctest --test-dir build-test --output-on-failure"
```

Expected: `connection_limiter`、`connection_proxy` 及既有测试全部通过。

- [ ] **Step 8: 提交**

```bash
git add chatbox/server/net/connection_limiter.h chatbox/server/net/connection_limiter.cpp \
  chatbox/server/net/acceptor.h chatbox/server/net/acceptor.cpp \
  chatbox/server/net/io_thread.h chatbox/server/net/io_thread.cpp \
  chatbox/server/main.cpp chatbox/tests/test_connection_limiter.cpp \
  chatbox/tests/test_connection_proxy.cpp chatbox/CMakeLists.txt
git commit -m "feat(net): limit connections by PROXY-derived client IP"
```

---

### Task 3: 构建、部署服务端并启用 500/50

**Files:**
- Modify: `chatbox/deploy/chatbox.service`
- Deploy: server source files changed by Task 1/2
- Backup: server binary and source under `/home/xxw/`

**Interfaces:**
- Consumes: Task 1/2 code.
- Produces: running `chatbox` with `--max-connections 500 --max-conn-per-ip 50 --proxy-protocol`.

**Steps:**

- [ ] **Step 1: 预检并备份**

```bash
ssh chatroom-dev "date -u; systemctl is-active chatbox nginx mysql; cd '/home/xxw/桌面/0917/chatbox' && cp -a . '/home/xxw/chatbox-src-backup-'$(date +%Y%m%d-%H%M%S) && cp -a build/chatbox_server '/home/xxw/chatbox_server-backup-'$(date +%Y%m%d-%H%M%S)"
```

Expected: 三个服务均为 `active`，备份路径存在。若源码备份因空间不足失败，停止并先清理旧的 `/home/xxw/chatbox-*backup*`，不要继续覆盖。

- [ ] **Step 2: 同步改动文件**

从 Windows 的 `D:\OpenCode_WorkSpace\chatbox\` 逐项 `scp` 到服务器 `/home/xxw/桌面/0917/chatbox/`，保持相对路径：

```text
server/auth/registration_rate_limiter.h
server/auth/registration_rate_limiter.cpp
server/auth/auth_service.h
server/auth/auth_service.cpp
server/business/worker_pool.h
server/business/worker_pool.cpp
server/net/connection_limiter.h
server/net/connection_limiter.cpp
server/net/acceptor.h
server/net/acceptor.cpp
server/net/io_thread.h
server/net/io_thread.cpp
server/main.cpp
tests/test_registration_rate_limiter.cpp
tests/test_connection_limiter.cpp
tests/test_connection_proxy.cpp
CMakeLists.txt
deploy/chatbox.service
```

复制后先对比关键文件，确认没有覆盖未知本地修改：

```bash
ssh chatroom-dev "cd '/home/xxw/桌面/0917/chatbox' && grep -n 'registration_rate_limiter\|rekey\|max-conn-per-ip' server/auth/auth_service.h server/business/worker_pool.h server/net/connection_limiter.h server/main.cpp deploy/chatbox.service"
```

Expected: 能看到新接口和 `--max-connections 500 --max-conn-per-ip 50`。

- [ ] **Step 3: 构建测试目录并运行全部测试**

```bash
ssh chatroom-dev "cd '/home/xxw/桌面/0917/chatbox' && rm -rf build-test && cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release && cmake --build build-test -j4 && ctest --test-dir build-test --output-on-failure"
```

Expected: `100% tests passed, 0 tests failed`。任何失败都停止，不重启线上服务。

- [ ] **Step 4: 更新 systemd 限制并构建生产二进制**

`deploy/chatbox.service` 的 `ExecStart` 必须为：

```ini
ExecStart=/home/xxw/桌面/0917/chatbox/tools/run_server.sh --listen 127.0.0.1 --port 9100 --io-threads 4 --worker-threads 4 --max-connections 500 --max-conn-per-ip 50 --proxy-protocol
```

执行：

```bash
ssh chatroom-dev "cd '/home/xxw/桌面/0917/chatbox' && cmake --build build -j4 && sudo cp deploy/chatbox.service /etc/systemd/system/chatbox.service && sudo systemctl daemon-reload && sudo systemctl restart chatbox && sleep 2 && systemctl is-active chatbox && journalctl -u chatbox -n 30 --no-pager"
```

Expected: `active`；日志出现 `server ready`，没有 `rekey`/`bind`/`database` 错误。若启动失败，立即用 Task 3 Step 1 的 `chatbox_server-backup-*` 和旧 unit 恢复，再 `systemctl restart chatbox`；不要让线上服务长时间停在失败状态。

- [ ] **Step 5: 局域网回归**

```bash
ssh chatroom-dev "echo | openssl s_client -connect 192.168.2.240:9443 -servername 192.168.2.240 -CAfile /etc/chatbox/certs/ca.crt -verify_ip 192.168.2.240 2>&1 | grep -E 'Verify return code|subject=|issuer='"
```

Expected: `Verify return code: 0 (ok)`。再用现有 Windows 客户端登录一次，确认房间列表和消息正常；客户端仍默认 `.240:9443`。

- [ ] **Step 6: 提交**

```bash
git add chatbox/deploy/chatbox.service
git commit -m "chore(deploy): enforce 500 global and 50 per-IP connection limits"
```

---

### Task 4: 证书 SAN 与 nginx 9444 隧道入口

**Files:**
- Modify: `chatbox/deploy/gen_certs.sh`
- Modify: `chatbox/deploy/nginx-chat-stream.conf`
- Modify: `chatbox/deploy/setup_nginx.sh`（仅当安装流程需要同步新文件时）
- Server: `/etc/chatbox/certs/chat.crt`, `chat.key`
- Server: `/etc/nginx/stream.d/chat.conf`

**Interfaces:**
- Produces: cert SAN `IP:192.168.2.240, DNS:chatbox.local, DNS:frp-oil.com`.
- Produces: nginx `127.0.0.1:9444 ssl proxy_protocol` and `0.0.0.0:9443 ssl`.

**Steps:**

- [ ] **Step 1: 修改 gen_certs.sh**

保留现有 CA 逻辑；在覆盖证书前备份：

```bash
if [ -f "${CERT_DIR}/chat.crt" ]; then
    sudo cp -a "${CERT_DIR}/chat.crt" "${CERT_DIR}/chat.crt.$(date +%Y%m%d-%H%M%S).bak"
fi
if [ -f "${CERT_DIR}/chat.key" ]; then
    sudo cp -a "${CERT_DIR}/chat.key" "${CERT_DIR}/chat.key.$(date +%Y%m%d-%H%M%S).bak"
fi
```

把默认值改为 `SAN_IP=${SAN_IP:-192.168.2.240}`、`SAN_DNS=${SAN_DNS:-chatbox.local,frp-oil.com}`。用逗号拆分 DNS 并构造：

```bash
san="IP:${SAN_IP}"
IFS=',' read -ra dns_names <<< "${SAN_DNS}"
for name in "${dns_names[@]}"; do
    san="${san},DNS:${name}"
done
```

`ext.cnf` 使用 `subjectAltName=${san}`。不要删除或重建 `ca.crt`/`ca.key`。

- [ ] **Step 2: 修改 nginx 配置**

保留原有 `9443` server，追加：

```nginx
log_format chatbox_stream '$remote_addr [$time_local] $protocol $status '
                          '$bytes_sent $bytes_received $session_time '
                          'proxy=$proxy_protocol_addr';
access_log /var/log/nginx/chatbox-stream.log chatbox_stream;

server {
    listen 127.0.0.1:9444 ssl proxy_protocol;
    ssl_certificate     /etc/chatbox/certs/chat.crt;
    ssl_certificate_key /etc/chatbox/certs/chat.key;
    ssl_protocols TLSv1.2 TLSv1.3;
    proxy_pass chat_backend;
    proxy_protocol on;
}
```

`9444` 不绑定 `0.0.0.0`；`9443` 不加 `proxy_protocol` 监听参数。`proxy_protocol on` 继续把真实地址传给后端。

- [ ] **Step 3: 部署证书并验证 SAN**

```bash
scp chatbox/deploy/gen_certs.sh chatroom-dev:/tmp/gen_certs.sh
ssh chatroom-dev "sudo SAN_IP=192.168.2.240 SAN_DNS=chatbox.local,frp-oil.com bash /tmp/gen_certs.sh && sudo openssl x509 -in /etc/chatbox/certs/chat.crt -noout -text | grep -A2 'Subject Alternative Name'"
```

Expected: 输出包含 `IP Address:192.168.2.240`、`DNS:chatbox.local`、`DNS:frp-oil.com`。

- [ ] **Step 4: 部署 nginx 并检查监听**

```bash
scp chatbox/deploy/nginx-chat-stream.conf chatroom-dev:/tmp/nginx-chat-stream.conf
ssh chatroom-dev "sudo cp /etc/nginx/stream.d/chat.conf /etc/nginx/stream.d/chat.conf.$(date +%Y%m%d-%H%M%S).bak 2>/dev/null || true; sudo cp /tmp/nginx-chat-stream.conf /etc/nginx/stream.d/chat.conf; sudo nginx -t && sudo systemctl reload nginx && ss -ltnp | grep -E ':(9443|9444)\b'"
```

Expected: `nginx -t` 成功；`9443` 监听 `0.0.0.0`/`*`，`9444` 只监听 `127.0.0.1`。日志文件存在。

- [ ] **Step 5: 回滚准备**

如果 `nginx -t` 失败，执行 `sudo cp /etc/nginx/stream.d/chat.conf.<timestamp>.bak /etc/nginx/stream.d/chat.conf && sudo systemctl reload nginx`，不要 reload 未通过测试的配置。证书失败则恢复 `chat.crt.*.bak`/`chat.key.*.bak` 并 reload。

- [ ] **Step 6: 提交**

```bash
git add chatbox/deploy/gen_certs.sh chatbox/deploy/nginx-chat-stream.conf
git commit -m "feat(deploy): add frp-oil SAN and loopback PROXY listener"
```

---

### Task 5: SakuraFrp frpc 与公网 TLS

**Files:**
- Create: `chatbox/deploy/sakurafrp.service`
- Server: `/usr/local/bin/frpc`
- Server: `/etc/chatbox/frpc.ini`（root 600）
- Server: `/etc/chatbox/sakurafrp.env`（仅临时，生成后删除）

**Interfaces:**
- Consumes: `frp-oil.com:15318`, local target `127.0.0.1:9444`, `proxy_protocol_version = v2`.
- Produces: `sakurafrp.service` active and public TLS endpoint.

**Steps:**

- [ ] **Step 1: 用户侧预检（不涉及 token 内容）**

在 SakuraFrp 面板确认并完成：

- 隧道类型 TCP，本地 `127.0.0.1:9444`。
- 自定义设置 `proxy_protocol_version = v2` 已保存。
- 访问密码和 TOTP 为空。
- 关闭 Windows/其他设备上正在运行的同一条隧道启动器，避免重复连接；服务器将成为唯一 frpc。

如果本地端口不是 9444，先在面板改成 9444 并重启隧道，再继续。

- [ ] **Step 2: 下载并校验 frpc**

从 SakuraFrp 管理面板“软件下载”获取 Linux amd64 的官方链接，然后在服务器执行（URL 由用户提供，不包含 token）：

```bash
ssh chatroom-dev "cd /tmp && wget -O frpc '<官方下载链接>' && chmod 755 frpc && ./frpc -V"
```

Expected: 版本号包含 `sakura`，且不低于 `0.51.0-sakura-14`。若面板提供 MD5/SHA 校验值，先 `md5sum /tmp/frpc`/`sha256sum /tmp/frpc` 对比，不一致停止。若版本号不含 `sakura`，停止，不使用上游二进制。随后 `sudo install -m 755 /tmp/frpc /usr/local/bin/frpc`。

- [ ] **Step 3: 用户放置 token 和隧道 ID**

由用户在服务器上执行（不要把值发到聊天）：

```bash
ssh chatroom-dev
sudo install -m 600 /dev/null /etc/chatbox/sakurafrp.env
sudo nano /etc/chatbox/sakurafrp.env
```

文件内容仅两行：

```ini
NATFRP_TOKEN=<访问密钥>
NATFRP_TARGET=<隧道ID>
```

保存后确认 `sudo stat -c '%a %U:%G %n' /etc/chatbox/sakurafrp.env` 为 `600 root:root`。

- [ ] **Step 4: 生成 root-only frpc 配置**

```bash
ssh chatroom-dev "sudo bash -c 'set -euo pipefail; set -a; . /etc/chatbox/sakurafrp.env; set +a; cd /etc/chatbox; /usr/local/bin/frpc -w; rm -f /etc/chatbox/sakurafrp.env; chmod 600 /etc/chatbox/frpc.ini; chown root:root /etc/chatbox/frpc.ini'"
ssh chatroom-dev "sudo grep -E '^(token|user|proxy_protocol_version|local_port|type|remote_port)' /etc/chatbox/frpc.ini | sed 's/^token = .*/token = <redacted>/; s/^user = .*/user = <redacted>/'"
```

Expected: 配置存在且 600；能看到 `proxy_protocol_version = v2`、`local_port = 9444`、`remote_port = 15318` 或等价字段。**不要** 打印 token。如果 `frpc -w` 没有读取环境变量，停止，不要用 `-f` 把 token 放进 argv；改用手工写入 root-only `frpc.ini` 或确认 frpc 版本的环境变量支持，token 只能存在于 600 文件中。

- [ ] **Step 5: 安装 systemd 服务**

`deploy/sakurafrp.service`：

```ini
[Unit]
Description=SakuraFrp ChatBox tunnel
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/local/bin/frpc -c /etc/chatbox/frpc.ini --disable_log_color
Restart=always
RestartSec=10
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=strict

[Install]
WantedBy=multi-user.target
```

部署：

```bash
scp chatbox/deploy/sakurafrp.service chatroom-dev:/tmp/sakurafrp.service
ssh chatroom-dev "sudo cp /tmp/sakurafrp.service /etc/systemd/system/sakurafrp.service && sudo systemctl daemon-reload && sudo systemctl enable --now sakurafrp && sleep 3 && systemctl is-active sakurafrp && journalctl -u sakurafrp -n 40 --no-pager"
```

Expected: `active`；日志显示连接节点/隧道启动成功。若 `-c` 不被该版本支持，改用 `WorkingDirectory=/etc/chatbox` 和无参数 `ExecStart`（frpc.ini 自动加载），重新测试。

- [ ] **Step 6: 公网 TLS 验证**

```bash
ssh chatroom-dev "echo | timeout 12 openssl s_client -connect frp-oil.com:15318 -servername frp-oil.com -CAfile /etc/chatbox/certs/ca.crt -verify_hostname frp-oil.com 2>&1 | grep -E 'Verify return code|subject=|issuer='"
```

Expected: `Verify return code: 0 (ok)`，subject/issuer 为 ChatBox Dev CA。随后用 Windows 客户端填写 `frp-oil.com` / `15318`，勾选 TLS，登录/收发一条消息。

- [ ] **Step 7: 失败处理**

- 隧道未启动：`systemctl status sakurafrp`、`journalctl -u sakurafrp`，检查 token、隧道 ID、本地端口 9444。
- TLS 握手失败/nginx 记录 PROXY error：确认面板 `proxy_protocol_version = v2` 已保存，且本地目标为 9444。
- 证书 SAN 错误：回 Task 4 重签，不重建 CA。
- 重复隧道冲突：关闭其他设备上的 frpc/启动器，只保留服务器。

- [ ] **Step 8: 提交**

```bash
git add chatbox/deploy/sakurafrp.service
git commit -m "feat(deploy): run SakuraFrp frpc as a systemd service"
```

---

### Task 6: 服务开机自启（不改电源、Wi-Fi、防火墙）

**Files:**
- No repository files; verification and `systemctl enable` only.

**Interfaces:**
- Consumes: `sakurafrp.service` from Task 5.
- Produces: `chatbox/nginx/mysql/sakurafrp` enabled at boot; UFW/logind/NetworkManager untouched.

**Steps:**

- [ ] **Step 1: 记录当前电源/网络/防火墙状态，不做修改**

```bash
ssh chatroom-dev "systemctl is-enabled chatbox nginx mysql 2>&1; systemctl is-active chatbox nginx mysql; sudo ufw status verbose 2>&1; loginctl show -p HandleLidSwitch 2>&1; iw dev wlp58s0 get power_save 2>&1"
```

Expected: 记录当前值。**不要**执行 `ufw enable`、不要改 `logind`、不要改 NetworkManager 或 Wi-Fi 省电；用户上课还要使用这台笔记本。

- [ ] **Step 2: 只启用服务开机自启**

```bash
ssh chatroom-dev "sudo systemctl enable chatbox nginx mysql sakurafrp && systemctl is-enabled chatbox nginx mysql sakurafrp && systemctl is-active chatbox nginx mysql sakurafrp"
```

Expected: 四个服务均 `enabled`、`active`。若 `sakurafrp` 尚未安装，先完成 Task 5，再执行本步。

- [ ] **Step 3: 确认没有改动系统电源/网络设置**

```bash
ssh chatroom-dev "sudo ufw status 2>&1; loginctl show -p HandleLidSwitch; iw dev wlp58s0 get power_save 2>&1"
```

Expected: UFW 仍为 `inactive`（或保持原状态）；`HandleLidSwitch` 和 Wi-Fi 省电与 Step 1 记录一致。**合盖/睡眠时公网会断开，这是用户明确接受的行为。**

- [ ] **Step 4: 不提交代码**

本任务不产生仓库文件；不创建 UFW/logind/NetworkManager 配置。把“未启用 UFW、未修改睡眠/Wi-Fi”写入 Task 8 的运维文档。

---

### Task 7: MySQL 8.4 每日备份与恢复演练

**Files:**
- Create: `chatbox/deploy/chatbox-backup.sh`
- Create: `chatbox/deploy/chatbox-backup.service`
- Create: `chatbox/deploy/chatbox-backup.timer`
- Server: `/usr/local/sbin/chatbox-backup.sh`
- Server: `/etc/chatbox/backup.cnf`（root 600）
- Server: `/var/backups/chatbox/`

**Interfaces:**
- Produces: daily gzip dump, 7-day retention, `chatbox_backup@localhost` account, restore drill.

**Steps:**

- [ ] **Step 1: 写备份脚本**

```bash
#!/usr/bin/env bash
set -euo pipefail
umask 077
backup_dir=/var/backups/chatbox
config=/etc/chatbox/backup.cnf
stamp=$(date -u +%Y%m%d-%H%M%S)
mkdir -p "${backup_dir}"
tmp="${backup_dir}/.chatbox-${stamp}.sql.gz.tmp"
trap 'rm -f "${tmp}"' EXIT
mysqldump --defaults-extra-file="${config}" \
    --single-transaction --quick --no-tablespaces --set-gtid-purged=OFF \
    --routines --triggers chatbox | gzip -9 > "${tmp}"
gzip -t "${tmp}"
mv "${tmp}" "${backup_dir}/chatbox-${stamp}.sql.gz"
find "${backup_dir}" -maxdepth 1 -type f -name 'chatbox-*.sql.gz' -mtime +7 -delete
```

`chatbox-backup.service`：

```ini
[Unit]
Description=ChatBox MySQL backup
After=mysql.service
Requires=mysql.service

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/chatbox-backup.sh
User=root
```

`chatbox-backup.timer`：

```ini
[Unit]
Description=Daily ChatBox MySQL backup

[Timer]
OnCalendar=*-*-* 03:15:00
Persistent=true
RandomizedDelaySec=10m

[Install]
WantedBy=timers.target
```

- [ ] **Step 2: 创建专用备份账号和凭据**

先 `ssh chatroom-dev` 登录服务器，再以 root 执行。密码只存在于 root shell 内存和 600 文件，不打印到终端：

```bash
ssh chatroom-dev
sudo bash <<'ROOT'
set -euo pipefail
umask 077
p=$(openssl rand -hex 24)
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
cat > "$tmp" <<SQL
CREATE USER IF NOT EXISTS 'chatbox_backup'@'localhost' IDENTIFIED BY '${p}';
ALTER USER 'chatbox_backup'@'localhost' IDENTIFIED BY '${p}';
GRANT SELECT, LOCK TABLES, SHOW VIEW, EVENT, TRIGGER ON chatbox.* TO 'chatbox_backup'@'localhost';
FLUSH PRIVILEGES;
SQL
mysql < "$tmp"
printf '[client]\nuser=chatbox_backup\npassword=%s\nhost=localhost\n' "$p" > /etc/chatbox/backup.cnf
chmod 600 /etc/chatbox/backup.cnf
chown root:root /etc/chatbox/backup.cnf
ROOT
```

Expected: `/etc/chatbox/backup.cnf` 为 `600 root:root`；`sudo mysql -e "SHOW GRANTS FOR 'chatbox_backup'@'localhost'"` 只显示 SELECT/LOCK TABLES/SHOW VIEW/EVENT/TRIGGER。若 `mysqldump` 报 `SHOW_ROUTINE` 权限错误，追加 `GRANT SHOW_ROUTINE ON *.* ...` 或去掉没有使用的 `--routines`；不要给备份账号全局 DML/DDL。

- [ ] **Step 3: 安装并手动运行**

```bash
scp chatbox/deploy/chatbox-backup.sh chatroom-dev:/tmp/chatbox-backup.sh
scp chatbox/deploy/chatbox-backup.service chatroom-dev:/tmp/chatbox-backup.service
scp chatbox/deploy/chatbox-backup.timer chatroom-dev:/tmp/chatbox-backup.timer
ssh chatroom-dev "sudo install -m 755 /tmp/chatbox-backup.sh /usr/local/sbin/chatbox-backup.sh && sudo cp /tmp/chatbox-backup.service /etc/systemd/system/ && sudo cp /tmp/chatbox-backup.timer /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl start chatbox-backup.service && sudo systemctl status chatbox-backup.service --no-pager && sudo ls -lh /var/backups/chatbox/"
```

Expected: service `result=success`；生成一个 `chatbox-*.sql.gz`。

- [ ] **Step 4: 恢复演练**

```bash
ssh chatroom-dev "latest=\$(ls -1t /var/backups/chatbox/chatbox-*.sql.gz | head -1); sudo mysql -e 'DROP DATABASE IF EXISTS chatbox_restore_test; CREATE DATABASE chatbox_restore_test;'; gunzip -c \"\$latest\" | sudo mysql chatbox_restore_test; sudo mysql -N -e 'SELECT COUNT(*) FROM chatbox_restore_test.users; SELECT COUNT(*) FROM chatbox_restore_test.rooms;'; sudo mysql -e 'DROP DATABASE chatbox_restore_test;'"
```

Expected: 两条计数都成功，临时库删除；生产库 `chatbox` 不受影响。若恢复失败，保留备份并检查 `mysqldump` 完整输出。

- [ ] **Step 5: 启用 timer**

```bash
ssh chatroom-dev "sudo systemctl enable --now chatbox-backup.timer && systemctl list-timers chatbox-backup.timer --no-pager"
```

Expected: timer `active (waiting)`，下次触发时间为次日 03:15 左右。

- [ ] **Step 6: 提交**

```bash
git add chatbox/deploy/chatbox-backup.sh chatbox/deploy/chatbox-backup.service chatbox/deploy/chatbox-backup.timer
git commit -m "feat(deploy): add daily MySQL backup and restore drill"
```

---

### Task 8: 客户端文档与公网使用说明

**Files:**
- Modify: `chatbox/README.md`
- Create: `chatbox/docs/public-access.md`

**Interfaces:**
- Consumes: public endpoint and limits.
- Produces: user-facing instructions without changing the portable ZIP or client code.

**Steps:**

- [ ] **Step 1: 更新 README 的运行参数**

把 TLS 代理运行的示例从 `--max-connections 20000 --max-conn-per-ip 0` 改为：

```text
--max-connections 500 --max-conn-per-ip 50
```

把“经过 nginx/FRP 时需设为 0”的说明改为：服务端现在在 PROXY v2 解析后按真实 IP 计数，`--max-conn-per-ip 50` 必须保留；不能改回 0。

- [ ] **Step 2: 更新证书说明**

把 SAN 行改为：

```text
IP:192.168.2.240、DNS:chatbox.local、DNS:frp-oil.com
```

把“gen_certs.sh 会重新生成 CA”改为“脚本只在 CA 缺失时生成；正常运行只重签服务器证书并保留 CA”。增加备份证书的说明。

- [ ] **Step 3: 新增公网说明**

`chatbox/docs/public-access.md` 内容至少包含：

```markdown
# ChatBox 公网访问

- 服务器：frp-oil.com
- 端口：15318
- TLS：勾选
- 客户端：使用现有 portable ZIP，必须保留 certs/ca.crt

首次连接在登录页填写上述地址；QSettings 会记住。局域网仍可用
192.168.2.240:9443。

注册开放；每 IP 每小时最多 20 次注册请求，全局 100 次；每 IP 最多
50 条连接。共享校园网出口按一个 IP 计。

SakuraFrp 免费额度约 5 GiB/月、10 Mbps；额度耗尽或节点维护时公网
不可用，局域网不受影响。笔记本合盖或睡眠时公网断开，唤醒后自动重连；
服务器不承诺 24 小时在线。不要分享 token、隧道 ID 或 CA 私钥。
```

- [ ] **Step 4: 说明备份和运维**

在 README 增加“公网部署与备份”小节，列出：

- `systemctl status chatbox nginx mysql sakurafrp`
- `journalctl -u sakurafrp`
- `ss -ltnp | grep -E ':(9443|9444|9100|3306)\b'`
- `/var/backups/chatbox/` 与恢复命令
- SakuraFrp 流量面板
- **UFW 未启用、合盖/睡眠/Wi-Fi 省电未修改**；合盖或睡眠时公网断开，服务只在笔记本醒着时可用

- [ ] **Step 5: 检查并提交**

```bash
git grep -n 'frp-oil.com\|15318\|max-conn-per-ip 50\|chatbox_backup' -- chatbox/README.md chatbox/docs/public-access.md
git add chatbox/README.md chatbox/docs/public-access.md
git commit -m "docs: add public SakuraFrp access and backup runbook"
```

Expected: README 不再出现“经过 nginx 时设为 0”的旧指引；公网说明可独立发给朋友。

---

### Task 9: 端到端验收与醒着窗口观察

**Files:**
- No production code changes; run verification only.

**Steps:**

- [ ] **Step 1: 服务与监听**

```bash
ssh chatroom-dev "systemctl is-active chatbox nginx mysql sakurafrp; ss -ltnp | grep -E ':(9443|9444|9100|3306)\b'; sudo ufw status 2>&1; loginctl show -p HandleLidSwitch"
```

Expected: 四个服务 active；`9444` 仅 127.0.0.1；`9443` 对局域网；UFW 保持未启用/原状态；合盖策略保持原值。这里不要求 UFW 规则。

- [ ] **Step 2: 证书与公网 TLS**

```bash
ssh chatroom-dev "echo | timeout 12 openssl s_client -connect frp-oil.com:15318 -servername frp-oil.com -CAfile /etc/chatbox/certs/ca.crt -verify_hostname frp-oil.com 2>&1 | grep -E 'Verify return code|subject='"
```

Expected: `Verify return code: 0 (ok)`，subject/SAN 含 `frp-oil.com`。从手机热点使用 Windows 客户端连接同一个端点，登录并发送一条消息；再从宿舍局域网连接 `.240:9443`，确认两条路径都可用。

- [ ] **Step 3: 真实 IP 与限流证据**

```bash
ssh chatroom-dev "sudo tail -n 20 /var/log/nginx/chatbox-stream.log; sudo journalctl -u chatbox -n 20 --no-pager; cd '/home/xxw/桌面/0917/chatbox' && sudo grep -c 'rate_limited' logs/chatbox.log || true"
```

Expected: 公网连接日志的 `proxy=` 不是 `127.0.0.1`；chatbox 日志没有 `invalid PROXY header`/`rekey rejected` 异常堆积。注册限流的第 21 次和连接限流的第 51 次由 Task 1/2 单元测试覆盖；不要在线上创建 20 个垃圾账号来做压力测试。若必须做实网验证，使用独立测试库和测试端口，不污染生产库。

- [ ] **Step 4: 备份和恢复**

```bash
ssh chatroom-dev "sudo systemctl start chatbox-backup.service; latest=\$(ls -1t /var/backups/chatbox/chatbox-*.sql.gz | head -1); gzip -t \"\$latest\" && echo BACKUP_GZIP_OK; sudo ls -lh /var/backups/chatbox/"
```

Expected: `BACKUP_GZIP_OK`，备份文件存在；Task 7 已做过临时库恢复。

- [ ] **Step 5: 重启验收**

在确认没有其他用户正在关键操作后执行：

```bash
ssh chatroom-dev "sudo reboot"
```

等待 2-3 分钟，重新连接，执行 Step 1 和 Step 2。Expected: 笔记本醒着时四个服务自动启动，公网和局域网都恢复；若合盖/睡眠，公网断开是预期行为，唤醒后重连。若没有恢复，查 `journalctl -b -u chatbox -u nginx -u sakurafrp`，不要反复重启。

- [ ] **Step 6: 醒着窗口观察**

用户上课会合盖/睡眠，不做 24 小时常开承诺。选一个笔记本保持醒着的 2 小时窗口，记录开始时间，结束后执行：

```bash
ssh chatroom-dev "uptime; systemctl is-active chatbox nginx mysql sakurafrp; df -h /; free -h; sudo journalctl -u sakurafrp --since '2 hours ago' --no-pager | tail -40; sudo tail -n 20 /var/log/nginx/chatbox-stream.log"
```

Expected: 服务仍 active；磁盘/内存无异常增长；没有持续重连/崩溃。记录断线次数和 SakuraFrp 面板流量；接近 5 GiB/月时提前决定限流、升级或停公网。合盖/睡眠导致的中断是预期行为，如实记录，不声称高可用。

- [ ] **Step 7: 记录撤销公网路径**

需要撤销公网时执行：

```bash
ssh chatroom-dev "sudo systemctl disable --now sakurafrp"
```

然后从 `/etc/nginx/stream.d/chat.conf` 删除/注释 `127.0.0.1:9444` server 块，执行 `sudo nginx -t && sudo systemctl reload nginx`。局域网 `9443` 不受影响；不要删除 CA 或重签证书。

---

## Plan Self-Review

- **Spec coverage:** 设计中的隧道、TLS/CA、真实 IP、注册限流、连接限流、备份、开机自启、客户端文档、验收均有对应任务；UFW/合盖/电源/Wi-Fi 明确不修改，睡眠时公网断开的限制已写入文档和验收。
- **Step scan:** 每个任务都有独立可验证的交付物；测试步骤给出断言，部署步骤给出 expected 输出。
- **Type consistency:** `RegistrationRateLimiter::allow(ip, now)`、`ConnectionLimiter::rekey(id, ip, error)`、`AuthService` 新构造参数、`IoThread::ProxyRekeyHandler` 在后续任务中保持同名同签名。
- **Review Focus tests:** 分片 PROXY v2、LOCAL、注册窗口边界、连接释放、SAN/loopback 分别在 Task 2、Task 1、Task 2、Task 4/9 有测试或验收。
- **Proportion:** 计划覆盖 9 个任务，代码部分只给接口和关键逻辑，未抄写完整实现。
