# Linux TCP 框架实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在远程 Linux 主机上交付一个可编译、可运行、可压测的 TCP 服务端框架：epoll ET、Acceptor + 多 IO 线程、二进制帧编解码、心跳超时、ping/pong 验证与优雅退出。

**Architecture:** 单进程多线程。Acceptor 独占监听 fd，用 `accept4` 接入后轮询分发给 IO 线程；每个 IO 线程持有独立 epoll、eventfd 和 timerfd，连接只属于一个 IO 线程。协议采用 14 字节大端帧头 + TLV 风格的二进制消息体；本阶段只实现 PING/PONG 与错误响应，业务处理函数以回调注入，后续接工作线程池。

**Tech Stack:** C++17、POSIX socket、epoll ET、pthread、eventfd、timerfd、CMake 3.16+、GCC 15（远程 Ubuntu 26.04）。本阶段不引入 spdlog/nlohmann/GoogleTest，使用标准库与内置断言测试，避免远程依赖阻塞。

**Spec:** `docs/2026-10-18-linux-chatroom-design.md`

**Status:** 已在远程主机实施并验证（2026-10-18）。

## Global Constraints

- C++17，编译参数 `-Wall -Wextra -O2`，固定宽度类型 `<cstdint>`。
- 网络字段一律大端，禁止直接收发内存结构体。
- 每个连接只属于一个 IO 线程；该连接的 `recv/send/close` 只在该线程执行。
- IO 线程内禁止数据库、磁盘等阻塞操作；本阶段无数据库。
- 客户端上行帧体上限 64 KB，服务端下行帧体上限 512 KB。
- 单连接写缓冲高水位 1 MB、硬上限 2 MB；超限断开。
- 目标目录：`/home/xxw/桌面/0917/chatroom`。
- 部署与操作只走 SSH；本机为 Windows，使用 Paramiko 执行远程命令和 SFTP。

## Review Focus

- 半包/粘包：一次 `recv` 可能拿到半个帧、多个帧或正好一帧，解析器必须正确区分“继续等”和“协议错误”。
- 超大/伪造长度：`body_len` 超过上限或帧头非法时必须断开，不能分配巨量内存。
- 慢客户端：对端不读导致写队列增长，必须触发水位保护而不是无限堆积。
- 关闭与 fd 复用：连接关闭后不得再收到 EPOLLOUT/EPOLLIN 回调；fd 关闭前先从 epoll 摘除。
- 优雅退出：收到 SIGTERM 后停止 accept、通知连接、等待在途任务、关闭全部 fd，不能挂死。

---

### Task 1: 工程骨架、配置与日志

**Files:**
- Create: `chatroom/CMakeLists.txt`
- Create: `chatroom/server/main.cpp`
- Create: `chatroom/server/config.h`
- Create: `chatroom/server/config.cpp`
- Create: `chatroom/server/util/logging.h`
- Create: `chatroom/server/util/logging.cpp`

**Interfaces:**
- Produces:
  - `struct Config { int port; int io_threads; int idle_timeout_sec; std::string log_file; LogLevel log_level; }`
  - `bool parseArgs(int argc, char** argv, Config& config, std::string& error)`
  - `void initLogging(const std::string& file, LogLevel level)`
  - `void logMessage(LogLevel level, const std::string& module, const std::string& message)`

- [ ] **Step 1: 写 CMake 骨架与 `main`，先只打印版本和解析参数**
- [ ] **Step 2: 在远程执行 `cmake -S . -B build && cmake --build build`，确认二进制生成**
- [ ] **Step 3: 运行 `./build/chatroom_server --help` 与非法参数，确认退出码和错误信息**
- [ ] **Step 4: 提交/同步到远程目录并在日志中记录开始**

### Task 2: 协议帧编解码

**Files:**
- Create: `chatroom/common/protocol.h`
- Create: `chatroom/common/protocol.cpp`
- Test: `chatroom/tests/test_protocol.cpp`
- Create: `chatroom/tests/test_framework.h`

**Interfaces:**
- Produces:
  - `constexpr uint16_t kProtocolMagic = 0x4348`
  - `struct FrameHeader { uint16_t magic; uint8_t version; uint8_t flags; uint16_t cmd; uint32_t seq; uint32_t body_len; }`
  - `struct Frame { FrameHeader header; std::string body; }`
  - `std::string encodeFrame(const Frame& frame)`
  - `bool decodeHeader(const char* data, size_t size, FrameHeader& header)`
  - `bool validateHeader(const FrameHeader& header, uint32_t max_body, std::string& error)`
  - `class ByteWriter { putU8/putU16/putU32/putString };`
  - `class ByteReader { getU8/getU16/getU32/getString };`

- [ ] **Step 1: 写失败测试：帧头往返、大端字节序、body 长度、非法 magic/version、超限 body**
- [ ] **Step 2: 运行测试确认失败（符号未定义）**
- [ ] **Step 3: 实现 `protocol.h/.cpp`，所有整数逐字段序列化**
- [ ] **Step 4: 运行 `test_protocol`，全部通过**
- [ ] **Step 5: 同步并提交**

### Task 3: 接收缓冲与帧提取

**Files:**
- Create: `chatroom/server/net/buffer.h`
- Create: `chatroom/server/net/buffer.cpp`
- Test: `chatroom/tests/test_buffer.cpp`

**Interfaces:**
- Produces:
  - `class Buffer { append(const char*, size_t); append(const std::string&); const char* data() const; size_t size() const; void consume(size_t); void clear(); }`
  - `enum class FrameParseResult { kNeedMore, kOk, kError }`
  - `FrameParseResult extractFrame(Buffer& buffer, Frame& frame, std::string& error)`

- [ ] **Step 1: 写失败测试：半包返回 `kNeedMore`、粘包连续提取两帧、非法帧返回 `kError`、consume/compact 正确**
- [ ] **Step 2: 运行测试确认失败**
- [ ] **Step 3: 实现 Buffer 与 `extractFrame`，读指针超过一半时压缩**
- [ ] **Step 4: 运行测试全部通过**
- [ ] **Step 5: 同步并提交**

### Task 4: 连接对象与 IO 线程

**Files:**
- Create: `chatroom/server/net/connection.h`
- Create: `chatroom/server/net/connection.cpp`
- Create: `chatroom/server/net/io_thread.h`
- Create: `chatroom/server/net/io_thread.cpp`

**Interfaces:**
- Produces:
  - `class Connection { using Ptr = std::shared_ptr<Connection>; int fd() const; uint64_t id() const; Buffer& input(); bool enqueue(std::string frame); bool flush(); void close(); bool closed() const; void touch(); }`
  - `class IoThread { using FrameHandler = std::function<void(const Connection::Ptr&, const Frame&)>; IoThread(int id, FrameHandler handler); void start(); void stop(); bool addConnection(int fd, uint64_t id); }`
  - `Connection` 的 `enqueue` 在超过 2 MB 时返回 false。

- [ ] **Step 1: 用 `socketpair` 写失败测试：写入一个完整帧后 `flush` 对端可读到；写队列超限返回 false**
- [ ] **Step 2: 实现 `Connection`，`send` 使用 `MSG_NOSIGNAL`，处理 `EAGAIN` 和部分写**
- [ ] **Step 3: 实现 `IoThread`：epoll + eventfd + timerfd，读循环到 `EAGAIN`，解析帧后回调**
- [ ] **Step 4: 与 Task 5 的 Acceptor 联调前，先编译通过**
- [ ] **Step 5: 同步并提交**

### Task 5: Acceptor、PING/PONG 与优雅退出

**Files:**
- Create: `chatroom/server/net/acceptor.h`
- Create: `chatroom/server/net/acceptor.cpp`
- Modify: `chatroom/server/main.cpp`
- Test: `chatroom/tools/tcp_client.py`（本地/远程联调脚本）

**Interfaces:**
- Produces:
  - `class Acceptor { Acceptor(int port, std::vector<IoThread*> io_threads); bool start(std::string& error); void stop(); }`
  - PING：`cmd=0x0040`，服务端回 `flags |= kFlagResponse`，`seq` 原样返回，body 原样返回。
  - 未知命令：`cmd=0x00FF`，`flags |= kFlagResponse | kFlagError`，body 为 TLV 错误码/文本（本阶段可用固定字符串）。

- [ ] **Step 1: 启动服务，Python 客户端连接并发送 PING，断言收到 PONG、seq 一致**
- [ ] **Step 2: 发送两个 PING，断言按序收到两个 PONG**
- [ ] **Step 3: 发送非法 magic，断言连接被服务端关闭**
- [ ] **Step 4: 发送 SIGTERM，断言进程在 5 秒内退出、端口释放**
- [ ] **Step 5: 同步并提交**

### Task 6: 远程部署与压测冒烟

**Files:**
- Create: `chatroom/tools/load_smoke.py`
- Create: `chatroom/README.md`

**Interfaces:**
- Produces:
  - 远程可执行：`/home/xxw/桌面/0917/chatroom/build/chatroom_server`
  - 冒烟标准：500 个并发连接，各发 1 次 PING，全部收到 PONG；进程 RSS < 200 MB；无 fd 泄漏。

- [ ] **Step 1: 在远程构建 Release 并跑 `ctest`**
- [ ] **Step 2: 启动服务，运行 500 连接冒烟脚本**
- [ ] **Step 3: 检查 `ss -ltnp`、日志、`/proc/<pid>/fd` 数量**
- [ ] **Step 4: 停止服务，确认端口释放**
- [ ] **Step 5: 更新 README 的构建、运行、测试命令**

---

## 实施验证结果（2026-10-18）

远程环境：Ubuntu 26.04、GCC 15.2、CMake 4.2.3、8 核 7 GB。
代码位置：`/home/xxw/桌面/0917/chatroom`。

| 验证项 | 结果 |
|---|---|
| Release 构建 | 通过，无编译错误 |
| `ctest` | 2/2 通过（protocol、buffer） |
| PING/PONG | 通过，seq/body 原样返回 |
| 粘包顺序 | 两个 PING 按序返回两个 PONG |
| 未知命令 | 返回 ERROR 响应 |
| 非法 magic | 服务端关闭连接 |
| 1 万并发连接冒烟 | 10,000 连接、每连接一次 PING，0 失败，0.93 s |
| 服务端内存 | 1 万连接测试后 RSS 约 13 MB |
| fd 回收 | 测试后回到 19 个 fd（无泄漏） |
| 心跳超时 | `--idle-timeout 3` 下 4.5 s 后连接被关闭 |
| 优雅退出 | SIGTERM 后进程退出、9100/9101 端口释放 |

当前测试实例运行在 `9100` 端口，启动方式：

```bash
cd /home/xxw/桌面/0917/chatroom
./tools/run_server.sh --port 9100 --io-threads 4
```**
