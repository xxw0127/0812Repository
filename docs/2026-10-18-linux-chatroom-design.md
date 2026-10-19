# Linux 高并发网络聊天室服务器 规划书

| 项目 | 内容 |
|---|---|
| 文档版本 | v1.1（评审修订） |
| 日期 | 2026-10-18 |
| 状态 | 待评审 |
| 目标读者 | 服务端开发、Qt 客户端开发、测试与运维 |
| 设计规模 | 单机 1 万在线连接，预留 2 倍余量 |
| 技术基线 | C++17、Linux（Ubuntu 20.04+）、epoll ET、MySQL 8.4、Qt 5.15 客户端 |

> 修订记录：v1.1 根据独立评审补充了大厅分厅、双向帧上限、连接内有序、`client_msg_id` 幂等、会话撤销与恢复、PROXY protocol、Argon2id 参数和私聊会话列表等决策。

> 本文是设计与实施规划，不是最终代码。文中代码片段仅用于说明协议布局、表结构和部署配置；目标平台为 Linux，编译器 GCC 9+，构建参数 `-Wall -Wextra -O2`。

---

## 1. 背景与目标

做一个面向公众开放的 TCP 长连接聊天室：任何人可以注册账号，登录后进入公共大厅或若干公开聊天室，发送群聊消息、与在线用户私聊、查看历史消息。服务端运行在 Linux 上，单机支撑约 1 万在线用户，客户为 Qt 桌面程序。

### 1.1 为什么做

- 需要一个能真实承载长连接、广播、持久化和并发压力的完整项目，而不是玩具级 echo server。
- 架构要能解释清楚每一个并发决策，可作为后续横向扩展的基线。

### 1.2 目标

1. 单机 1 万 TCP 长连接稳定在线，空闲时 CPU 占用低、内存可控。
2. 群聊广播在 500 人房间内 P99 推送延迟 < 200 ms。
3. 注册、登录、房间、私聊、在线列表、历史消息全部可用。
4. 消息落库 MySQL 8.4，服务重启后历史可查。
5. 7×24 小时压测无句柄泄漏、无内存持续增长。
6. 服务端代码平台宏只存在于网络/线程抽象层，业务层保持平台无关。

### 1.3 非目标（v1 明确不做）

- 语音、视频、文件传输、图片消息。
- 好友关系、离线消息主动推送、消息已读回执。
- 举报、屏蔽、禁言、敏感词过滤等内容运营能力。
- 端到端加密、阅后即焚、消息撤回。
- 多机集群、跨服聊天、联邦协议。
- Web 端、移动端。
- 用户自建房间（v1 房间由管理端/初始化 SQL 配置）。

### 1.4 反目标（即使功能能跑，也算失败）

- 广播时在 IO 线程里同步查库或做磁盘 IO。
- 为了省事让客户端直连 MySQL。
- 直接 `send` 内存结构体，依赖编译器对齐和本机字节序。
- 慢客户端拖垮整个房间，写缓冲无上限。
- 单条 SQL 字符串拼接，存在注入面。
- 连接数一上去就靠加线程硬扛，线程数随连接数增长。

---

## 2. 需求范围

### 2.1 功能清单（v1）

| 编号 | 功能 | 说明 |
|---|---|---|
| F1 | 注册 | 用户名 + 密码 + 昵称，用户名唯一 |
| F2 | 登录 / 登出 | 密码校验，签发 Token；单账号单会话 |
| F3 | 公共大厅 | 自动分厅，每厅上限 1000 人；满员后进入下一个分厅 |
| F4 | 多聊天室 | 预置若干公开房间，可加入/退出 |
| F5 | 群聊 | 房间内广播文本消息，单条 ≤ 2000 字符 |
| F6 | 私聊 | 给在线用户发私聊，双方都能查历史与会话列表 |
| F7 | 在线列表 | 查看当前房间在线成员 |
| F8 | 历史消息 | 按游标分页拉取群聊/私聊历史 |
| F9 | 心跳与超时 | 30 秒心跳，90 秒无活动断开 |
| F10 | 断线重连 | 客户端带 Token 重连，恢复会话 |
| F11 | 限流 | 登录尝试、发消息频率限制 |
| F12 | 运维观测 | 结构化日志、周期统计、优雅退出 |
| F13 | 私聊会话列表 | 最近私聊对象与最后一条消息（v1 不含未读数） |

### 2.2 用户故事

- 作为新用户，我可以注册一个账号并立即登录。
- 作为登录用户，我能看到房间列表，进入大厅并看到最近 50 条消息。
- 作为房间成员，我发的消息能被房间内其他人看到，延迟不明显。
- 作为用户，我可以私聊另一个在线用户，对方离线时消息仍保留在历史里。
- 作为运维，我可以通过日志和统计判断当前连接数、消息速率和异常。

---

## 3. 约束与假设

### 3.1 硬约束

- 服务端：Linux，C++17，原生 POSIX socket + epoll（ET 优先），pthread。
- 数据库：MySQL 8.4，仅服务端访问，预处理语句，禁止拼接 SQL。
- 客户端：Qt 5.15，Windows 为主；网络层使用原生 Winsock/POSIX socket，不直连数据库。
- 通信：TCP 长连接，网络字节序，二进制序列化，禁止结构体直发。
- 部署：仅通过 SSH（scp/rsync/git over ssh）传输与操作，禁止 FTP/SMB/U 盘。

### 3.2 假设

- 单机部署：1 台应用服务器 + 1 台 MySQL（可与应用同机，生产建议分离）。
- 公网入口前置 TLS 终结（nginx stream），并开启 PROXY protocol（v1/v2 自动识别）传递真实客户端 IP；应用只监听回环/内网地址，v1 收明文 TCP。内网/测试环境可直连并关闭 PROXY protocol。
- 聊天内容为纯文本 UTF-8，不做富文本渲染。
- 用户规模 10 万注册量以内，日活 1 万以内。
- 消息峰值 200 条/秒，历史查询峰值 100 次/秒。

### 3.3 技术选型

| 组件 | 选择 | 理由 |
|---|---|---|
| 网络模型 | 多线程 Reactor，每 IO 线程独立 epoll | 避免多线程争抢同一 epoll，连接归属清晰 |
| 并发模型 | IO 线程 + 业务工作线程池分离 | IO 回调不阻塞，DB 操作不拖慢收发 |
| 协议 | 自定义二进制帧 + TLV 消息体 | 无外部依赖，字段边界明确，便于抓包排查 |
| 数据库访问 | MySQL C API + 每工作线程独立连接 | 符合 MySQL 8.4 C API 线程约束 |
| 密码哈希 | libargon2（Argon2id） | 参数可控，抗暴力破解，避免自造加密 |
| 日志 | spdlog（同步/异步可配） | 分级、滚动、格式统一 |
| 配置 | JSON + nlohmann/json | 结构清晰，单头文件依赖 |
| 构建 | CMake 3.16+ | 服务端与测试统一；客户端仍用 qmake |
| 测试 | GoogleTest + 自研压测工具 | 单测与协议级压测 |

依赖清单：`pthread`、`libargon2-dev`、`libmysqlclient-dev`、`nlohmann-json3-dev`、`libspdlog-dev`、`libgtest-dev`。

---

## 4. 容量规划

### 4.1 规模指标

| 指标 | 设计值 | 说明 |
|---|---|---|
| 同时在线连接 | 10,000 | 架构预留 20,000 |
| 注册用户 | 100,000 | 单表足够 |
| 聊天室数量 | 100 | 上限；v1 预置 12 个（大厅 10 + 技术交流 + 闲聊） |
| 单房间人数上限 | 1,000 | 防止广播风暴；大厅按此上限拆为 10 个分厅 |
| 群聊峰值 | 200 条/秒 | 全局 |
| 单房间群聊峰值 | 50 条/秒 | 500 人房间压测口径 |
| 私聊峰值 | 50 条/秒 | 全局 |
| 历史查询 | 100 次/秒 | 每次 ≤ 50 条 |
| 单条消息 | ≤ 2,000 字符 | UTF-8，约 6 KB 上限 |

### 4.2 资源估算

| 资源 | 估算 | 依据 |
|---|---|---|
| 单连接内存 | 约 10–20 KB 常态，最坏 2 MB 写缓冲 | 8 KB 读缓冲 + 写队列 + 连接状态；写缓冲受全局预算约束 |
| 1 万连接内存 | 约 0.5–1.5 GB | 含共享广播帧与对象池开销 |
| 出站带宽 | 峰值约 8 MB/s | 全局 200 条/秒，按平均 100 人扇出估算 |
| 应用服务器 | 4 核 8 GB | 4 IO 线程 + 8 工作线程 |
| MySQL | 4 核 8 GB，buffer pool 2 GB | 写入 250 条/秒量级 |

> 结论：1 万在线不需要多机或消息中间件；瓶颈更可能出现在广播扇出和慢客户端，而不是 epoll 本身。

---

## 5. 总体架构

### 5.1 逻辑拓扑

```
┌──────────────┐    TCP/TLS    ┌──────────────────────────────────────┐
│ Qt 客户端     │◄────────────►│  Linux 聊天服务器                     │
│ (Windows)    │              │                                      │
└──────────────┘              │  Acceptor ──► IO 线程 × 4（epoll ET） │
                              │                    │                 │
                              │                    ▼                 │
                              │            业务工作线程池 × 8          │
                              │             │        │        │      │
                              │             ▼        ▼        ▼      │
                              │          会话管理   房间管理   消息持久化│
                              │                              │       │
                              └──────────────────────────────┼───────┘
                                                             ▼
                                                      MySQL 8.4
```

### 5.2 进程与线程模型

单进程多线程：

| 线程 | 数量 | 职责 |
|---|---|---|
| 主线程 | 1 | 启动、信号处理、优雅退出协调 |
| Acceptor | 1 | `accept4` 非阻塞接入，轮询分发给 IO 线程 |
| IO 线程 | 4 | 各自持有 epoll，负责读写、编解码、心跳超时扫描 |
| 工作线程 | 8 | 鉴权、房间操作、SQL、消息路由，可阻塞 |
| 定时任务 | 复用 IO 线程 timerfd | 每秒扫描超时连接、刷新统计 |

关键规则：

1. **一个连接只属于一个 IO 线程**，连接上的 `read/write/close` 只在该线程执行，天然避免 socket 跨线程并发。
2. Acceptor 将新 fd 通过 `eventfd + SPSC 队列` 投递给目标 IO 线程，不在 Acceptor 里读写业务数据。
3. IO 线程解析出完整帧后，按 `conn_id % worker_count` 固定路由到对应工作线程队列；工作线程的响应再投回该连接所属 IO 线程。
4. 工作线程不直接操作 socket，只提交“向某连接发送某帧”的任务。
5. 房间状态由 `RoomManager` 管理，按 room_id 分片加锁；广播时在锁内取快照，锁外投递。
6. **同一连接的帧固定由同一工作线程按 FIFO 处理**，保证 `LOGIN→JOIN→CHAT`、连续 `CHAT_SEND` 不会因并发而乱序。代价是单连接上的慢查询会阻塞该用户后续请求，这个代价可以接受；不同连接之间仍然并行。

### 5.3 模块划分

```
server/
├── net/          Acceptor、IOThread、EpollPoller、Buffer、Timerfd
├── protocol/     Frame 编解码、TLV Reader/Writer、命令字
├── session/      连接状态机、SessionManager、Token 校验
├── service/      注册、登录、房间、群聊、私聊、历史查询
├── room/         RoomManager、成员表、广播扇出
├── db/           MySQL 连接池、DAO、预处理语句封装
├── util/         日志、配置、限流器、随机数、时间
└── main.cpp      装配与生命周期
```

---

## 6. 通信协议

### 6.1 帧格式

所有整数使用网络字节序（大端），固定 14 字节帧头：

```
偏移  长度  字段        说明
0     2     magic       0x4348（"CH"）
2     1     version     协议版本，v1 = 1
3     1     flags       bit0=响应, bit1=压缩(保留), bit2=错误
4     2     cmd         命令字
6     4     seq         请求序号，响应原样带回；推送为 0
10    4     body_len    消息体长度，0 ~ 524288（按方向限制）
14    N     body        TLV 编码的消息体
```

规则：

- 方向不对称上限：客户端→服务端 `body_len ≤ 65536`；服务端→客户端 `body_len ≤ 524288`。后者足以容纳 50 条 × 6 KB 的历史页或 1000 人成员列表；超限立即断开并记 WARN。
- 客户端读缓冲按 512 KB 上限设计；服务端写队列水位单独限制（见 §9.3），二者不是同一个阈值。
- 未认证连接只允许 `REGISTER/LOGIN/HEARTBEAT`，其他命令返回 1002。
- 帧头字段逐字段序列化，禁止 `memcpy` 整个结构体。
- TCP 是字节流，必须处理半包/粘包：读缓冲循环解析，`body_len` 不足时等待下一次可读。

### 6.2 TLV 消息体

```
type  uint16   字段类型
len   uint16   值长度
value bytes    值
```

- 字符串统一 UTF-8，不带结尾 `\0`。
- 未知字段跳过，保证协议向后兼容。
- 单字段长度上限由消息类型约束；文本消息内容 ≤ 2000 字符。

### 6.3 命令字

| cmd | 名称 | 方向 | 主要字段 |
|---|---|---|---|
| 0x0001 | REGISTER_REQ / RESP | C→S / S→C | username, password, nickname → user_id |
| 0x0002 | LOGIN_REQ / RESP | C→S / S→C | username, password → token, user_id, nickname |
| 0x0003 | LOGOUT_REQ / RESP | C→S / S→C | — |
| 0x0004 | RESUME_REQ / RESP | C→S / S→C | token → user_id, nickname |
| 0x0010 | ROOM_LIST_REQ / RESP | C→S / S→C | → room_id, name, online_count |
| 0x0011 | JOIN_ROOM_REQ / RESP | C→S / S→C | room_id |
| 0x0012 | LEAVE_ROOM_REQ / RESP | C→S / S→C | room_id |
| 0x0013 | ROOM_MEMBERS_REQ / RESP | C→S / S→C | room_id → user_id, nickname |
| 0x0020 | CHAT_SEND_REQ | C→S | room_id, client_msg_id, content |
| 0x0021 | CHAT_SEND_RESP | S→C | server_msg_id, created_at |
| 0x0022 | CHAT_PUSH | S→C | room_id, server_msg_id, sender, content, created_at |
| 0x0030 | PRIVATE_SEND_REQ | C→S | to_user_id, client_msg_id, content |
| 0x0031 | PRIVATE_SEND_RESP | S→C | server_msg_id, created_at |
| 0x0032 | PRIVATE_PUSH | S→C | from_user_id, nickname, content, created_at |
| 0x0033 | PRIVATE_CONVERSATIONS_REQ / RESP | C→S / S→C | → peer_id, nickname, last_msg_id, last_content, last_time, online |
| 0x0040 | HEARTBEAT_REQ / RESP | 双向 | 时间戳 |
| 0x0041 | SYSTEM_NOTICE | S→C | 用户加入/退出、服务器公告 |
| 0x0050 | HISTORY_REQ / RESP | C→S / S→C | room_id 或 peer_id, before_id, limit → items, has_more, next_before_id |
| 0x00FF | ERROR_RESP | S→C | code, message |

### 6.4 错误码

| code | 含义 | 客户端处理 |
|---|---|---|
| 0 | 成功 | — |
| 1001 | 参数非法 | 提示，不重试 |
| 1002 | 未认证 / Token 无效 | 跳转登录 |
| 1003 | 用户名已存在 | 提示更换 |
| 1004 | 用户名或密码错误 | 提示，计数限流 |
| 1005 | 操作过于频繁 | 退避重试 |
| 1006 | 房间不存在 / 已满 | 刷新列表 |
| 1007 | 不在该房间 | 先加入 |
| 1008 | 目标用户不存在 | 提示 |
| 1009 | Token 过期 | 重新登录 |
| 1010 | 账号在其他设备登录 | 提示被踢下线 |
| 1011 | 消息过长 | 截断提示 |
| 2001 | 服务器内部错误 | 提示并上报日志 |
| 2002 | 服务正在关闭 | 等待重连 |

### 6.5 连接状态机

```
CONNECTED ──LOGIN/RESUME 成功──► AUTHENTICATED ──LOGOUT/错误/超时──► CLOSING
     │                                  │
     └──10 秒未认证──► CLOSING          └──进入/退出房间只改房间状态
```

- `CONNECTED` 超时 10 秒。
- `AUTHENTICATED` 心跳间隔 30 秒，90 秒无任何帧判定超时。
- `CLOSING` 先 `shutdown(SHUT_WR)`，尝试发完错误帧，再 `close`，从 epoll 摘除并释放会话。

---

## 7. 核心流程

### 7.1 注册

1. 客户端发送 `REGISTER_REQ`，密码在客户端只做长度与字符校验，不哈希。
2. IO 线程解码后投递工作线程。
3. 工作线程校验用户名格式（3–32 位，字母数字下划线）、昵称长度、密码强度（8–128 位）。
4. 用 Argon2id（19 MiB / t=2 / p=1）+ 16 字节随机盐生成哈希，`INSERT INTO users`；唯一键冲突返回 1003。
5. 返回 `user_id`，不自动登录（客户端随后发 `LOGIN_REQ`）。

### 7.2 登录

1. `LOGIN_REQ(username, password)`。
2. 工作线程按用户名查 `users`（预处理语句），校验 Argon2id。
3. 失败则累加 IP + 用户名维度的失败计数，5 次/分钟触发 1005。
4. 成功则生成 32 字节随机 Token，DB 存 `SHA-256(token)`，内存缓存 `token_hash → session`。
5. 单账号单会话规则：
   - 新登录成功时，把该用户的旧会话（排除刚生成的新 Token）标记 `revoked = 1`，内存缓存同步失效；
   - 通过 `user_id → connection` 映射找到旧连接，向旧连接推送 1010，并向其所属 IO 线程投递关闭任务；
   - 旧连接关闭原因记为 `kicked_by_new_login`，房间成员关系随之清理。
6. 返回 `token, user_id, nickname`；连接置为 `AUTHENTICATED`。
7. Token 有效期 7 天；登出时把当前会话置为 `revoked = 2`。服务端优先查内存缓存，未命中查 DB。
8. 同一用户的登录/恢复在 `SessionManager` 内按 `user_id` 分片加锁串行化，避免并发登录产生两个有效会话。

> Token 放 TLV 里而不是每帧头，保持帧头精简；实现上在认证后由连接对象绑定会话，后续帧可不再重复携带。

### 7.3 加入房间与群聊广播

加入：

1. `JOIN_ROOM_REQ(room_id)`。
2. 工作线程检查房间存在、人数未满；大厅分厅满员返回 1006，客户端自动尝试下一个未满分厅；通过后将 `(user_id, connection_id)` 加入 `RoomManager` 成员表。
3. 加入成功后向房间广播 `SYSTEM_NOTICE`（“xxx 加入房间”），历史由客户端显式拉取。
4. 重复加入幂等。

群聊：

1. 客户端发 `CHAT_SEND_REQ(room_id, client_msg_id, content)`。
2. IO 线程校验帧长、内容长度，投递工作线程。
3. 工作线程校验：已认证、在房间内、限流通过。
4. 写库：`INSERT INTO messages`，取得 `server_msg_id` 与 `created_at`。
5. 组装一次 `CHAT_PUSH` 帧为 `shared_ptr<const vector<char>>`。
6. 在房间锁内取成员连接快照，锁外按“IO 线程”分组投递。
7. 每个 IO 线程把共享帧追加到目标连接的发送队列，必要时注册 `EPOLLOUT`。
8. 向发送者返回 `CHAT_SEND_RESP`，带 `server_msg_id`，客户端可用它做本地消息确认。

顺序保证：

- `server_msg_id` 是 MySQL 全局单调递增的自增 ID，作为唯一消息顺序键；历史与实时推送使用同一字段。
- 不同工作线程并发写库后，广播到达顺序可能短暂乱序；客户端维护 200 ms 重排窗口，按 `server_msg_id` 升序展示。
- 同一发送者的连续消息由“按连接固定路由 + 队列 FIFO”保证先后顺序。
- 不引入房间内 `room_seq`，避免历史按 ID、实时按序号出现两套顺序。

### 7.4 私聊

1. `PRIVATE_SEND_REQ(to_user_id, client_msg_id, content)`。
2. 校验目标存在、非自己、内容合法、限流通过。
3. 写入 `private_messages`，取得消息 ID。
4. 若目标在线：向目标连接推送 `PRIVATE_PUSH`；否则仅落库（v1 不做离线推送）。
5. 向发送者返回 `PRIVATE_SEND_RESP`。
6. 会话发现：`PRIVATE_CONVERSATIONS_REQ` 返回最近私聊对象、最后一条消息与时间；v1 不统计未读数，保证离线收到的私聊在下次登录后可见。

### 7.5 心跳与超时

- 客户端每 30 秒发 `HEARTBEAT_REQ`，服务端立即回 `HEARTBEAT_RESP`。
- 每个 IO 线程持有 `timerfd`，每秒触发一次，扫描本线程连接的最后活跃时间。
- 超过 90 秒未收到任何完整帧：标记 `CLOSING`，记录原因 `idle_timeout`，关闭连接并从房间移除。
- 心跳不查库、不投递工作线程，直接在 IO 线程回复。

### 7.6 断线重连

1. 客户端指数退避重连（1s、2s、4s，上限 30s）。
2. 连接建立后发 `RESUME_REQ(token)`。
3. 服务端校验 Token 未过期且 `sessions.revoked = 0`：
   - 若该 Token 对应的旧连接仍然存在（半开连接、网络切换），执行接管：向旧连接投递关闭任务（原因 `resume_takeover`），新连接绑定同一会话；
   - 若无旧连接，直接恢复。
4. Token 已撤销时按原因区分：被新登录撤销（`revoked = 1`）返回 1010，主动登出（`revoked = 2`）返回 1002；Token 过期返回 1009，格式非法返回 1002。客户端清理本地 Token 回登录页。
5. 恢复成功后客户端按本地记录的房间列表逐个重新 `JOIN_ROOM`（允许同时在多个房间），服务端不保存“上次房间”状态。

### 7.7 历史消息

请求：`HISTORY_REQ(room_id, before_id=0, limit=50)`。

```sql
SELECT id, sender_id, content, created_at
FROM messages
WHERE room_id = ? AND id < ?
ORDER BY id DESC
LIMIT ?;
```

- `before_id = 0` 时绑定 `BIGINT UNSIGNED` 最大值（18446744073709551615），语义为取最新一页；返回结果在服务端反转为升序，同时保持 `(room_id, id)` 索引范围扫描。
- `limit` 上限 50，超过则截断；响应带 `has_more` 与 `next_before_id`，用于继续翻页。
- 单页响应最坏约 300 KB（50 条 × 6 KB），落在服务端→客户端 512 KB 帧上限内。
- 私聊历史按 `(sender_id, receiver_id)` 双向查询，两列联合索引支撑。
- 私聊历史强制以当前会话 `user_id` 作为查询一端，客户端只能提供 `peer_id`，不能指定 `sender_id`，防止越权读取。
- 群聊历史要求当前用户已加入该房间，防止遍历任意房间。

---

## 8. 数据库设计

### 8.1 表结构

```sql
-- 用户
CREATE TABLE users (
    id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username        VARCHAR(32)     NOT NULL,
    nickname        VARCHAR(32)     NOT NULL,
    password_hash   VARCHAR(255)    NOT NULL,
    status          TINYINT         NOT NULL DEFAULT 0 COMMENT '0正常 1禁用',
    created_at      DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    last_login_at   DATETIME(3)     NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uk_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- 房间（v1 由初始化脚本预置）
CREATE TABLE rooms (
    id          INT UNSIGNED    NOT NULL AUTO_INCREMENT,
    name        VARCHAR(32)     NOT NULL,
    max_members INT UNSIGNED    NOT NULL DEFAULT 1000,
    created_at  DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (id),
    UNIQUE KEY uk_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- 群聊消息
CREATE TABLE messages (
    id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    room_id     INT UNSIGNED    NOT NULL,
    sender_id   BIGINT UNSIGNED NOT NULL,
    client_msg_id VARCHAR(36)   NOT NULL COMMENT '客户端 UUID v4，用于幂等',
    msg_type    TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0文本',
    content     VARCHAR(2000)   NOT NULL,
    created_at  DATETIME(3)     NOT NULL,
    PRIMARY KEY (id),
    KEY idx_room_id_id (room_id, id),
    KEY idx_created_at (created_at),
    UNIQUE KEY uk_room_sender_client (room_id, sender_id, client_msg_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- 私聊消息
CREATE TABLE private_messages (
    id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    sender_id   BIGINT UNSIGNED NOT NULL,
    receiver_id BIGINT UNSIGNED NOT NULL,
    client_msg_id VARCHAR(36)   NOT NULL COMMENT '客户端 UUID v4，用于幂等',
    content     VARCHAR(2000)   NOT NULL,
    created_at  DATETIME(3)     NOT NULL,
    PRIMARY KEY (id),
    KEY idx_sender_receiver (sender_id, receiver_id, id),
    KEY idx_receiver_sender (receiver_id, sender_id, id),
    UNIQUE KEY uk_pair_sender_client (sender_id, receiver_id, client_msg_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- 会话令牌（只存哈希）
CREATE TABLE sessions (
    token_hash  CHAR(64)        NOT NULL,
    user_id     BIGINT UNSIGNED NOT NULL,
    expires_at  DATETIME        NOT NULL,
    created_at  DATETIME        NOT NULL,
    revoked     TINYINT         NOT NULL DEFAULT 0 COMMENT '0有效 1被新登录撤销 2主动登出',
    PRIMARY KEY (token_hash),
    KEY idx_user_id (user_id),
    KEY idx_expires_at (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
```

初始化房间（大厅按 1000 人上限拆分，预留 1 万容量）：

```sql
INSERT INTO rooms (name, max_members) VALUES
('大厅 1', 1000), ('大厅 2', 1000), ('大厅 3', 1000), ('大厅 4', 1000),
('大厅 5', 1000), ('大厅 6', 1000), ('大厅 7', 1000), ('大厅 8', 1000),
('大厅 9', 1000), ('大厅 10', 1000),
('技术交流', 500), ('闲聊', 500);
```

`client_msg_id` 为客户端生成的 UUID v4 字符串（36 字符）。同一发送者在同一房间（或同一私聊对）内唯一；重复请求命中唯一键时，查询已有记录并原样返回 `server_msg_id`，不重复写库、不重复广播。这样超时重发和崩溃恢复都不会产生重复消息。

### 8.2 索引与容量

- `messages` 的 `(room_id, id)` 覆盖“按房间倒序取一页”的主查询。
- `private_messages` 建双向索引，私聊历史查询都能走索引。
- `(room_id, sender_id, client_msg_id)` 与 `(sender_id, receiver_id, client_msg_id)` 唯一索引承担发送幂等；冲突时回查已有记录并返回原消息编号。
- 会话列表查询按“我发出的”和“我收到的”两路取每个对端的最新 ID；数据量增长后再引入 `private_conversations` 汇总表。
- 单条消息 2,000 字符约 2–6 KB，按 250 条/秒、单条 1 KB 估算：约 21 GB/天；v1 默认保留 90 天，需约 2 TB 磁盘，实际按内容长度和速率复核。
- 若日增消息超过 500 万行，优先采用“按月归档表 + 在线表只保留近期数据”的方案，而不是直接分区；MySQL 8.4 要求所有唯一键都包含分区列，现有 `client_msg_id` 幂等唯一键与按时间分区存在冲突，必须在实施前重新评审。这是 v1 预留的演进点，不默认开启。

### 8.3 连接池与访问规范

- 每个工作线程持有 1 个 `MYSQL*` 连接，**禁止跨线程共用**；线程启动时建连，断线自动重连并重建预处理语句。
- 连接初始化执行 `SET NAMES utf8mb4`，设置 `time_zone = '+00:00'`。
- 所有 SQL 使用 `mysql_stmt_prepare` + `mysql_stmt_bind_param`；禁止字符串拼接。
- 每次调用检查返回值，失败记录 `mysql_errno` / `mysql_error`，向上返回错误码。
- `MYSQL_RES*` / `MYSQL_STMT*` 用 RAII 包装，任何分支都释放。
- 事务只用于注册等需要原子性的场景；聊天写入单条 INSERT，不开显式事务。
- 数据库操作全部在工作线程执行，IO 线程零 SQL。

### 8.4 数据保留与备份

- 消息默认保留 90 天，每日凌晨分批 `DELETE ... LIMIT 5000`，避免长事务锁表。
- `mysqldump` 每日全量 + binlog 增量，备份文件通过 SSH 传输到备份机。
- `sessions` 过期记录每日清理；Token 有效期 7 天。

---

## 9. 高并发与稳定性设计

### 9.1 epoll ET 要点

1. 监听 fd 设置 `SO_REUSEADDR`；监听 fd 与连接 fd 全部设为非阻塞（`accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)`）。
2. ET 模式下读事件必须循环 `recv` 直到 `EAGAIN`，否则边缘触发不再通知。
3. 写事件按需注册：发送队列非空且上次 `send` 返回 `EAGAIN` 时才 `EPOLL_CTL_MOD` 加 `EPOLLOUT`；队列清空后立即移除。
4. 关注 `EPOLLRDHUP`、`EPOLLHUP`、`EPOLLERR`，统一走关闭流程。
5. `accept` 也循环到 `EAGAIN`，避免连接堆积。
6. 收到 `EINTR` 重试；`EPOLLOUT` 与 `EPOLLIN` 在同一连接上可能同时就绪，先读后写。
7. 所有 fd 在关闭前先从 epoll 摘除，防止同一 fd 复用后事件串线。
8. 发送使用 `send(..., MSG_NOSIGNAL)`，进程启动时忽略 `SIGPIPE`，避免对端关闭导致进程被信号终止。

### 9.2 跨线程投递

- Acceptor → IO 线程：每个 IO 线程一个 `eventfd` + 任务队列，Acceptor 轮询分配。
- IO 线程 → 工作线程：按 `conn_id % worker_count` 固定路由到 8 个有界 FIFO 队列，任务包含 `connection_id + frame`；同一连接只进同一个队列，天然保证连接内顺序。
- 工作线程之间不窃取对方队列的任务（窃取会破坏连接内顺序）；用连接哈希均衡负载，必要时改为一致性哈希。
- 工作线程 → IO 线程：按 `io_thread_id` 投递，避免工作线程直接写 socket。
- 连接对象生命周期用 `shared_ptr` + `weak_ptr`：IO 线程持有强引用，工作线程任务持有 `weak_ptr`，执行前 `lock()` 校验连接是否已关闭。
- 每个任务队列设上限（如 10 万条），超限触发背压：暂停读取该连接或拒绝新请求，禁止无限堆积。

### 9.3 广播与背压

- 广播帧只序列化一次，所有接收者共享 `shared_ptr<const vector<char>>`，避免 1 万次拷贝。
- 按 IO 线程分组投递，减少跨线程队列竞争。
- 每连接写队列高水位 1 MB：超过后标记 slow consumer，丢弃非关键推送（如系统通知）；超过 2 MB 或持续 30 秒未回落则断开。
- 全局写缓冲预算 1 GB；耗尽时按队列长度从大到小断开慢连接，避免 1 万连接同时堆积导致 OOM。
- 聊天消息不无限缓存：背压期间宁可断开慢客户端，也不拖累房间。
- 房间成员表用读写锁或分片锁，广播只做快照，不在锁内做 send。
- 单房间上限 1000 人；“大厅”在 v1 中直接拆为 10 个预置分厅（大厅 1–10），客户端默认进入未满的第一个，避免单个万人房间的广播风暴。

### 9.4 内存管理

- 读缓冲固定 8 KB，按需扩容到单帧上限；写队列按块（shared_ptr）追加，支持 `writev` 散射写。
- 高频小对象（连接对象、任务、帧）使用对象池，减少 malloc 抖动。
- 所有缓冲设硬上限，禁止因客户端行为无限增长。
- 连接关闭时清空读写队列、从房间移除、释放会话引用；用 RAII 保证异常路径也释放。

### 9.5 定时器

- 每个 IO 线程一个 `timerfd`（1 秒周期），扫描本线程连接的最后活跃时间。
- 心跳超时、认证超时、slow consumer 回收均复用该扫描，不额外起线程。
- 避免为每个连接创建一个定时器，1 万连接下定时器堆的维护成本不划算。

### 9.6 优雅退出

1. 收到 `SIGTERM/SIGINT`，主线程置停止标志，关闭监听 fd。
2. 向所有在线连接推送 `ERROR_RESP(2002, "server shutting down")`。
3. 停止接收新任务，等待工作线程处理完当前队列（最多 5 秒）。
4. 关闭所有连接（先 `shutdown` 再 `close`），释放 MySQL 连接池。
5. 写退出日志，进程以 0 退出；超时未完成则强制退出并记录未完成任务数。

---

## 10. 安全设计

| 风险 | 措施 |
|---|---|
| 密码泄露 | Argon2id（19 MiB、t=2、p=1）+ 每用户随机盐；数据库不存明文 |
| Token 泄露 | 只存 `SHA-256(token)`；有效期 7 天；支持登出失效 |
| SQL 注入 | 全部预处理语句；DAO 层禁止拼接 SQL |
| 明文传输 | 公网必须 TLS 终结；应用只监听内网/回环，由 nginx stream 转发 |
| 代理后 IP 失真 | nginx stream 开启 PROXY protocol（v1/v2 自动识别）；应用只信任 `trusted_proxies` 网段，解析失败回退 socket 对端 IP |
| 暴力登录 | IP + 用户名双维度失败计数，5 次/分钟锁定 5 分钟 |
| 消息刷屏 | 每连接令牌桶：10 条/秒，突发 20；超限返回 1005 |
| 超大帧 | 客户端上行帧体上限 64 KB；服务端下行 512 KB 由服务端自身生成，不受客户端控制 |
| 慢速攻击 | 认证超时 10 秒、空闲超时 90 秒、读缓冲上限 |
| 连接耗尽 | 单 IP 最大 20 个连接（按 PROXY protocol 解析出的真实 IP）；全局最大连接数 20,000；压测环境可配置为 0 关闭 |
| 内容注入 | 服务端只做长度/UTF-8/控制字符过滤；客户端用纯文本控件渲染，禁止富文本 |
| 账号枚举 | 登录失败统一返回 1004，不区分“用户不存在/密码错误” |

补充：

- 配置中的数据库密码从环境变量读取，不写入仓库。
- 日志禁止打印密码、Token 原文、完整消息内容（可打印长度与哈希前缀）。
- 管理/统计接口只监听 Unix domain socket，权限 0600。

---

## 11. 可观测性

### 11.1 日志

- 级别：DEBUG / INFO / WARN / ERROR。
- 格式：`时间 线程ID 级别 模块 事件 关键字段`。
- 必记事件：启动配置摘要、监听成功、登录成功/失败（不含密码）、连接关闭原因、DB 错误、限流触发、广播丢弃、优雅退出。
- 滚动：单文件 100 MB，保留 7 天；日志目录 `/opt/chatroom/logs`。

示例：

```
2026-10-18 12:00:01.123 [io-2] INFO  session login ok uid=10086 ip=10.0.0.8 conn=4821
2026-10-18 12:00:02.456 [worker-3] WARN room broadcast slow conn=5120 queued=1200000 action=drop_notice
```

### 11.2 统计指标

每 10 秒输出一行统计日志，或通过 Unix socket 查询：

| 指标 | 说明 |
|---|---|
| online_connections | 当前连接数 |
| authenticated_users | 已登录用户数 |
| msgs_in / msgs_out | 收发消息计数 |
| broadcast_frames / broadcast_drops | 广播次数与丢弃数 |
| db_errors | DB 错误累计 |
| io_queue_depth / worker_queue_depth | 队列深度 |
| rss_kb / open_fds | 进程内存与句柄 |

### 11.3 告警阈值（部署后配置）

- 连接数 > 18,000 持续 5 分钟。
- 工作队列深度 > 50,000 持续 1 分钟。
- DB 错误率 > 1%。
- 进程 RSS > 3 GB 或打开 fd > 30,000。

---

## 12. 部署方案

### 12.1 目录结构

```
/opt/chatroom/
├── bin/chatroom_server
├── conf/server.json
├── logs/
└── run/chatroom.pid
/etc/chatroom/chatroom.env     # DB 密码等敏感配置，权限 0600
```

部署前创建专用系统用户：

```bash
sudo useradd --system --home /opt/chatroom --shell /usr/sbin/nologin chatroom
sudo chown -R chatroom:chatroom /opt/chatroom
```

### 12.2 构建

```bash
# 在开发机或服务器上执行，目标 Ubuntu 20.04+，GCC 9+
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
(cd build && ctest --output-on-failure)
sudo install -m 0755 build/server/chatroom_server /opt/chatroom/bin/
```

编译参数由 CMake 统一注入：`-Wall -Wextra -O2 -DNDEBUG`；Debug 构建追加 `-fsanitize=address,undefined`。

### 12.3 systemd

```ini
[Unit]
Description=Chatroom Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=chatroom
Group=chatroom
WorkingDirectory=/opt/chatroom
ExecStart=/opt/chatroom/bin/chatroom_server -c /opt/chatroom/conf/server.json
Restart=on-failure
RestartSec=3
LimitNOFILE=65535
EnvironmentFile=-/etc/chatroom/chatroom.env
Environment=TZ=UTC
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

### 12.4 SSH 发布流程

```bash
# 1. 本地构建并跑单测
cmake --build build -j && (cd build && ctest --output-on-failure)

# 2. 仅通过 SSH 传输二进制与配置
rsync -avz --rsync-path="sudo rsync" -e ssh build/server/chatroom_server deploy@server:/opt/chatroom/bin/
rsync -avz --rsync-path="sudo rsync" -e ssh conf/server.json deploy@server:/opt/chatroom/conf/

# 3. 远程重启并观察日志
ssh deploy@server 'sudo systemctl restart chatroom && sudo systemctl status chatroom --no-pager'
ssh deploy@server 'tail -f /opt/chatroom/logs/chatroom.log'
```

要求：部署账号为普通用户 + sudo；SSH 使用 ed25519 密钥；禁止 root 直登；禁止 FTP/SMB/U 盘传文件。

### 12.5 内核与资源参数

```conf
# /etc/security/limits.d/chatroom.conf
chatroom soft nofile 65535
chatroom hard nofile 65535
```

```conf
# /etc/sysctl.d/99-chatroom.conf
net.core.somaxconn = 32768
net.ipv4.tcp_max_syn_backlog = 16384
net.ipv4.tcp_tw_reuse = 1
net.ipv4.ip_local_port_range = 10240 65000
fs.file-max = 200000
```

> 不开启 `tcp_tw_recycle`（NAT 环境有坑）。

### 12.6 nginx stream 入口（TLS + PROXY protocol）

```nginx
stream {
    upstream chat_backend {
        server 127.0.0.1:9100;
    }

    server {
        listen 9443 ssl;
        ssl_certificate     /etc/nginx/certs/chat.crt;
        ssl_certificate_key /etc/nginx/certs/chat.key;
        proxy_pass chat_backend;
        proxy_protocol on;   # 向后端传递真实客户端 IP
    }
}
```

- 应用只监听 `127.0.0.1:9100`，安全组不对外开放。
- nginx stream 的 `proxy_protocol on` 发送 PROXY v1；应用 `auto` 同时兼容 v1/v2，便于将来切换 HAProxy 等入口。
- 应用侧 `trusted_proxies` 只配置代理机地址；来自非可信地址的 PROXY 头直接拒绝。
- 压测环境直连应用时把 `proxy_protocol` 设为 `off`，并把 `max_conn_per_ip` 设为 `0` 关闭单 IP 限制。

---

## 13. Qt 客户端概要

### 13.1 结构

```
client/
├── ui/          登录页、主窗口、房间列表、聊天视图、私聊标签
├── core/        SocketClient、ProtocolCodec、SessionStore、ReconnectPolicy
└── models/      消息列表模型、在线用户模型
```

### 13.2 线程模型

- UI 全部在主线程。
- 网络层放在独立 `QThread`：使用原生 Winsock（Windows）/POSIX socket，阻塞 `select` 或非阻塞轮询均可；收到完整帧后通过信号槽把 `QByteArray` 投回主线程。
- Windows 下 `winsock2.h` 必须位于 `windows.h` 之前；启动时 `WSAStartup(MAKEWORD(2,2))`，退出时 `WSACleanup()`；socket 关闭统一走 `shutdown` → `closesocket`。
- `SocketClient` 对象 `moveToThread(netThread)`，所有 socket 操作只在该线程执行。
- 主线程只发“发送请求”信号，不直接操作 socket；禁止跨线程直接改 QObject 成员。
- 断线重连、心跳定时器在收到网络线程事件后由主线程决策，再投递命令回网络线程。

### 13.3 UI 页面

| 页面 | 内容 |
|---|---|
| 登录/注册 | 用户名、密码、昵称；错误提示；记住用户名（不记密码） |
| 主窗口 | 左侧房间列表 + 在线人数，中间消息流，底部输入框 |
| 私聊 | 双击在线用户或从最近私聊列表打开标签页 |
| 最近私聊 | 显示最近私聊对象、最后一条消息与时间（v1 不含未读数） |
| 历史 | 向上滚动加载更早消息（游标 `before_id`） |
| 状态栏 | 连接状态、当前账号、延迟 |

### 13.4 客户端规则

- Token 存 `QSettings`（或系统凭据存储），密码不落盘。
- 发送消息先本地回显为“发送中”，收到 `CHAT_SEND_RESP` 后标记成功；超时标记失败可重发（每条消息生成 UUID v4 作为 `client_msg_id`，服务端幂等去重）。
- 消息渲染使用纯文本，链接可点击但不解析 HTML。
- 重连期间禁用发送按钮，恢复后重新拉取当前房间最近消息。

---

## 14. 工程结构

```
chatroom/
├── CMakeLists.txt
├── common/            # 协议编解码、命令字、错误码（客户端可复用）
├── server/
│   ├── net/
│   ├── protocol/
│   ├── session/
│   ├── service/
│   ├── room/
│   ├── db/
│   └── util/
├── client/            # Qt qmake 工程
├── tools/loadgen/     # 协议级压测工具
├── tests/             # GoogleTest 单测 + 集成测试
├── deploy/            # systemd、sysctl、初始化 SQL
└── docs/
```

构建目标：

- `chatroom_server`
- `chatroom_loadgen`
- `chatroom_tests`
- `client/chatroom_client.pro`（qmake，独立构建）

---

## 15. 里程碑

| 阶段 | 周期 | 交付物 | 验收 |
|---|---|---|---|
| M1 骨架与协议 | 第 1 周 | CMake 工程、日志/配置、帧与 TLV 编解码、单测 | 编解码单测通过，半包/粘包用例覆盖 |
| M2 网络层 | 第 2 周 | Acceptor、4 个 IO 线程、连接生命周期、心跳、1 万空闲连接压测 | 1 万连接稳定 1 小时，无 fd 泄漏 |
| M3 账号体系 | 第 3 周 | MySQL 连接池、注册/登录/Token、限流 | 登录 50 次/秒（突发 100），错误码正确 |
| M4 聊天核心 | 第 4 周 | 房间、群聊广播、私聊、在线列表、历史查询 | 500 人房间 50 条/秒、全局 200 条/秒，P99 < 200 ms |
| M5 客户端 | 第 5 周 | Qt 登录、房间、聊天、私聊、重连 | 双客户端互通，断网恢复 |
| M6 加固 | 第 6 周 | 背压、慢客户端、优雅退出、安全加固 | 故障注入与 24 小时稳定性测试通过 |
| M7 部署验收 | 第 7 周 | systemd、部署脚本、运维文档、压测报告 | 生产环境灰度上线 |

> 周期按 1 名熟悉 C++/Linux 的开发者全职投入估算；并行投入客户端可压缩 M5。

---

## 16. 测试与验收

### 16.1 测试层次

| 层次 | 内容 | 工具 |
|---|---|---|
| 单元测试 | 帧编解码、TLV 边界、环形缓冲、令牌桶、超时扫描 | GoogleTest |
| 集成测试 | 注册→登录→加入→发言→历史，私聊双向 | 测试用客户端 + 测试库 |
| 协议健壮性 | 超大帧、负长度、截断帧、乱序字段、随机字节 | libFuzzer/AFL |
| 压力测试 | 1 万连接、全局 200 条/秒广播、100 次/秒历史 | `tools/loadgen` |
| 故障注入 | kill -9、MySQL 重启、网络延迟/丢包、磁盘满 | tc netem、脚本 |
| 安全测试 | SQL 注入尝试、暴力登录、Token 伪造、慢速攻击 | 脚本 + 人工 |

### 16.2 验收标准

1. **容量**：1 万连接在线，空闲 CPU < 20%（4 核），RSS < 2.5 GB。
2. **延迟**：500 人房间 50 条/秒（全局 200 条/秒），P99 推送 < 200 ms，P99.9 < 500 ms。
3. **吞吐**：登录 50 次/秒持续 5 分钟、突发 100 次/秒持续 30 秒，成功率 > 99.9%；Argon2id 参数为 19 MiB / t=2 / p=1，实测不足时增加核数或独立鉴权线程，不降低到 OWASP 最低参数以下。
4. **稳定**：24 小时压测，RSS 波动 < 10%，fd 数量无持续增长，无崩溃。
5. **恢复**：`kill -9` 后 systemd 3 秒内拉起，客户端 30 秒内自动重连。
6. **数据**：重启后历史消息可查；同一 `client_msg_id` 重发不产生重复记录；客户端以 `server_msg_id` 去重展示。
7. **安全**：预处理语句覆盖全部 SQL；密码/Token 不出现在日志；超限连接被拒绝。
8. **部署**：按文档从零部署成功，`systemctl status` 正常，日志无 ERROR。

---

## 17. 风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| ET 模式漏读导致连接假死 | 消息延迟/断连 | 读循环到 EAGAIN；单测覆盖半包；压测监控 `EPOLLIN` 次数 |
| 广播扇出成为瓶颈 | 大房间延迟升高 | 共享帧 + 按 IO 线程分组；房间上限 1000；必要时拆厅 |
| 慢客户端拖垮内存 | OOM | 写队列高水位 + 丢弃策略 + 断开 |
| MySQL 单点 | 登录/历史不可用 | v1 单机 + 备份；后续主从/读写分离 |
| 消息表增长过快 | 查询变慢、磁盘满 | 90 天保留、分批删除、预留归档表方案 |
| 密码/Token 泄露 | 账号安全 | Argon2id、Token 哈希存储、TLS、日志脱敏 |
| 客户端跨线程操作 QObject | 随机崩溃 | 网络线程 + 信号槽回主线程，代码评审强制检查 |
| 协议版本升级不兼容 | 老客户端不可用 | 帧头 version + TLV 未知字段跳过 |
| 压测工具本身成为瓶颈 | 结论失真 | 压测机独立部署，监控压测机 CPU/网络 |
| Argon2id 参数过高 | 登录吞吐不足 | 19 MiB / t=2 / p=1，登录独立限流；实测不足时加核或拆分鉴权服务 |
| 代理未传真实 IP | 限流失效或误封 | PROXY protocol v1/v2 + 可信网段校验，日志记录解析结果 |

---

## 18. 默认决策与待确认项

以下是规划阶段做出的默认选择，评审时可直接改：

| 项 | 默认决策 | 影响 |
|---|---|---|
| 协议编码 | 自定义二进制 + TLV，不引入 protobuf | 无外部依赖，但需要自己维护兼容性 |
| 会话策略 | 单账号单会话，新登录踢旧连接 | 多端同时在线需要改为多会话表 |
| 房间管理 | v1 预置房间，不支持用户自建 | 自建房间需增加权限与审核 |
| 离线消息 | 不主动推送，保留私聊历史与最近会话列表 | 未读数与主动推送放 v1.1 |
| 传输安全 | 应用侧明文，公网由 TLS 终结 | 若要求端到端加密，需要重新设计 |
| 消息保留 | 90 天 | 合规或永久保留需调整归档策略 |
| 部署形态 | 单机单进程 | 多机需要网关 + 路由层 |
| 数据库版本 | MySQL 8.4（按远程环境调整，原设计为 5.7） | 使用 caching_sha2_password；SQL 保持 5.7 兼容写法，便于回退 |
| 客户端网络 | 原生 socket + 独立线程 | 与 Qt 自带 QTcpSocket 方案不同，按项目规范执行 |
| 大厅形态 | 10 个预置分厅，单厅上限 1000 | 单一万人厅需要发布订阅/分片广播，v1 不做 |
| 消息顺序 | `server_msg_id` 为唯一顺序键，客户端 200 ms 重排 | 不引入 `room_seq`，避免历史与实时两套顺序 |
| 发送幂等 | `client_msg_id` UUID + 唯一索引，重复请求返回原消息 | 客户端必须按消息生成 UUID，服务端不回放广播 |
| 帧上限 | C→S 64 KB，S→C 512 KB | 历史页与成员列表一帧返回，不做应用层分片 |
| 鉴权参数 | Argon2id 19 MiB / t=2 / p=1，登录 50 次/秒 | 更高参数需要更多 CPU 或独立鉴权服务 |
| 代理 IP | PROXY protocol v1/v2 + `trusted_proxies` | 不解析代理头会导致 per-IP 限流失效 |
| 私聊发现 | 最近会话列表，不含未读数 | 未读数需要读状态表，放 v1.1 |
| 会话恢复 | 同 Token 重连接管旧连接；被新登录撤销返回 1010，主动登出返回 1002 | 需要 `sessions.revoked` 与跨 IO 线程关闭能力 |

需要业务方确认的问题：

1. 聊天室是否允许匿名游客？当前方案要求注册登录。
2. 是否需要用户自建房间和房间管理员？当前为预置房间。
3. 消息保留期与是否需要导出/审计？
4. 公网 TLS 证书由谁提供，是否接受 nginx stream 终结？
5. 是否接受“大厅 1–10”的分厅形态与命名？若必须单一万人大厅，需要改为发布订阅/分片广播架构。
6. 是否需要多端同时在线（PC + 手机）？当前单会话，新登录会踢掉旧连接。

---

## 附录 A：协议交互示例（登录）

```
C→S  Frame{ magic=0x4348, ver=1, flags=0, cmd=0x0002, seq=1,
             body=TLV(username="alice") + TLV(password="******") }

S→C  Frame{ magic=0x4348, ver=1, flags=0x01, cmd=0x0002, seq=1,
             body=TLV(user_id=10086) + TLV(nickname="Alice")
                  + TLV(token="<64 hex chars>") }

C→S  Frame{ cmd=0x0011(JOIN_ROOM), seq=2, body=TLV(room_id=1) }
S→C  Frame{ cmd=0x0011, seq=2, body=TLV(ok=1), flags=0x01 }
S→C  Frame{ cmd=0x0041(SYSTEM_NOTICE), seq=0,
             body=TLV(text="Alice 加入房间") }

C→S  Frame{ cmd=0x0020(CHAT_SEND), seq=3,
             body=TLV(room_id=1) + TLV(client_msg_id="550e8400-e29b-41d4-a716-446655440000")
                  + TLV(content="大家好") }
S→C  Frame{ cmd=0x0021, seq=3, body=TLV(server_msg_id=90001) }
S→C  Frame{ cmd=0x0022(CHAT_PUSH), seq=0,
             body=TLV(room_id=1) + TLV(server_msg_id=90001)
                  + TLV(sender_id=10086) + TLV(nickname="Alice")
                  + TLV(content="大家好") + TLV(created_at=...) }
```

## 附录 B：关键配置示例

```json
{
  "server": {
    "listen": "127.0.0.1",
    "port": 9100,
    "io_threads": 4,
    "worker_threads": 8,
    "max_connections": 20000,
    "max_conn_per_ip": 20,
    "proxy_protocol": "auto",
    "trusted_proxies": ["127.0.0.1/32", "10.0.0.0/8"]
  },
  "db": {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "chatroom",
    "database": "chatroom",
    "password_env": "CHATROOM_DB_PASSWORD"
  },
  "auth": {
    "token_ttl_days": 7,
    "argon2_memory_kb": 19456,
    "argon2_iterations": 2,
    "argon2_parallelism": 1,
    "login_rate_per_sec": 50,
    "login_burst": 100
  },
  "limits": {
    "auth_timeout_sec": 10,
    "heartbeat_interval_sec": 30,
    "idle_timeout_sec": 90,
    "max_in_frame_bytes": 65536,
    "max_out_frame_bytes": 524288,
    "write_high_water_bytes": 1048576,
    "write_hard_limit_bytes": 2097152,
    "write_budget_bytes": 1073741824,
    "msg_rate_per_sec": 10,
    "msg_burst": 20
  }
}
```
