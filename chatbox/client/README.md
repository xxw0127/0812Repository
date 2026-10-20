# ChatBox 客户端（Qt 5.15）

产品名 **ChatBox**；目录、可执行文件（`chatbox.exe`）、QSettings 键与服务端 systemd 服务名在 2026-10-19 已全部统一为 `chatbox`/`ChatBox`。启动时会把旧的 `Chatroom/ChatroomClient` 设置一次性迁移到 `ChatBox/ChatBox`，已有登录会话和设置不受改名影响。

Windows Qt 5.15.2 Widgets 客户端，原生 Winsock + 独立网络线程。

## 环境

```text
Qt:      D:\Qt\5.15.2\mingw81_64
MinGW:   D:\Qt\Tools\mingw810_64
OpenSSL: C:\Program Files\OpenSSL-Win64
```

客户端默认连接 `192.168.2.240:9443`，开启 TLS 并校验 `certs\ca.crt`。

## 构建

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client
.\build_client.ps1
```

脚本会编译并把 Qt DLL、MinGW 运行库、OpenSSL DLL、`platforms` 插件、`imageformats` 插件、`sqldrivers` 插件和 `certs` 复制到 `build\release`。

`imageformats\qjpeg.dll` 是 JPEG 头像编解码的运行时依赖；脚本会自动创建
`build\release\imageformats` 并复制该插件。缺少它时头像上传/显示会回退或失败。

`Qt5Sql.dll` 与 `sqldrivers\qsqlite.dll` 是本地聊天记录库（SQLite）的运行时依赖；
脚本会一并复制到 `build\release` 和 `build\release\sqldrivers`。缺少它们时客户端
仍能启动，但本地历史功能整体降级为「不可用」（详见下文「本地聊天记录与离线模式」）。

如果客户端正在运行，先关闭 `chatbox.exe`，否则链接会失败。

## 分发包

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client
.\package_client.ps1
```

生成：

```text
dist\chatbox-0.2.0-win64.zip
```

ZIP 内含 exe、Qt/MinGW/OpenSSL DLL、`vcruntime140.dll`、`certs`、`platforms`、`styles`、
`imageformats`、`sqldrivers` 和 README，可直接解压运行。
`installer\chatbox.iss` 是 Inno Setup 模板；当前机器未安装 Inno Setup，所以只生成便携 ZIP。
Windows 10/11 自带 UCRT；更旧的系统需要安装 VC++ 2015-2022 Redistributable。

> 便携包必须包含 `Qt5Sql.dll`、`sqldrivers\qsqlite.dll` 和 `imageformats\qjpeg.dll`。
> `package_client.ps1` 在打包后会逐个校验这三项，缺任何一项直接报错、不产出 ZIP；
> 缺 SQLite 驱动时程序仍能启动，但本地历史整体不可用。

> 用 Qt Creator 直接构建时，qmake 会复制 `certs`，但不会复制 OpenSSL DLL 或
> Qt `imageformats` / `sqldrivers` 插件。如果运行时提示缺少 `libssl-4-x64.dll` /
> `libcrypto-4-x64.dll`，或头像无法解码，或本地历史提示缺少 SQLite 驱动，
> 先执行一次 `build_client.ps1`，或手动把
> `C:\Program Files\OpenSSL-Win64\bin` 下的两个 DLL、
> `D:\Qt\5.15.2\mingw81_64\plugins\imageformats\qjpeg.dll` 和
> `D:\Qt\5.15.2\mingw81_64\plugins\sqldrivers\qsqlite.dll`
> 复制到 `build\release` 对应目录。

## 运行

```powershell
.\build\release\chatbox.exe
```

默认服务器地址：`192.168.2.240:9443`，TLS 已勾选。

## TLS 证书

客户端校验证书链和 IP SAN，CA 在：

```text
D:\OpenCode_WorkSpace\chatbox\client\certs\ca.crt
```

服务端证书 SAN 为 `IP:192.168.2.240, DNS:chatbox.local`。服务器 LAN 地址固定为 `192.168.2.240/24`；如果 IP 或域名变化，需要重签服务器证书（复用原 CA 时客户端 `ca.crt` 无需更换），否则客户端会拒绝连接。

## 个人资料

- 左栏底部个人资料栏（头像、昵称、UID）点击打开自己的主页。
- 好友列表点击头像、好友右键「查看资料」、群成员右键「查看资料」、私聊顶部头像、用户搜索结果和好友申请对话框都可打开资料页（会话头部只有「头像 + 标题 + ⋮」，没有单独的「资料」按钮）。
- 他人主页显示头像、备注优先的标题、昵称、`@username`、UID、在线状态、签名和好友操作按钮；自己的主页可以编辑昵称（1–32 个 Unicode 字符）、签名（0–128 个字符），以及更换/移除头像。
- 头像选择支持 JPEG/PNG：客户端居中裁成正方形、缩放到 256×256、JPEG quality 85；超过 48 KiB 时降到 quality 75/65 和 192×192、128×128，仍超限则提示失败。
- 上传或清除头像都会递增服务端 `avatar_version`；客户端按 `user_id + version` 缓存。没有头像、清除头像或解码失败时回退为彩色首字母头像。
- 在线好友收到 `PROFILE_UPDATE_PUSH` 后更新好友列表昵称/头像；有备注时列表和聊天顶部仍优先显示备注。
- 修改昵称后自己的资料栏、资料页、会话 `QSettings` 和后续发送消息立即使用新昵称，不覆盖当前私聊/房间标题。
- JPEG 头像依赖 Qt `imageformats\qjpeg.dll`；`build_client.ps1` 会把它复制到 `build\release\imageformats`。
- 生产环境 `192.168.2.240:9443` 已部署包含资料命令的新服务端，当前客户端可自动恢复会话并在服务重启后重连。

## 消息操作

- 鼠标悬停消息气泡时，气泡旁的空白处出现浮动操作条：他人消息只有「回复」，自己的消息在 2 分钟窗口内有「回复 / 撤回」，已撤回或超出窗口的消息不显示「撤回」。
- 右键消息气泡弹出菜单：失败消息有「重发」，他人消息有「回复 / 复制」，自己的消息在窗口内有「回复 / 撤回 / 复制」；空白处保留系统自带的复制/全选菜单。
- 「回复」把引用放进输入区，回车发送后新消息带引用行；「撤回」窗口与服务端一致（消息发出 2 分钟内），超出窗口的消息客户端不再提供入口，服务端仍会二次校验。
- 操作条只在悬停期间出现，不占用常驻空间，也不再使用蓝色下划线链接；悬停重入同一条消息、滚动、切换会话时都会收起。

## 本地聊天记录与离线模式

客户端在本机维护一份聊天记录副本（SQLite），**服务端仍然是唯一权威来源**；本地库只是离线时的只读镜像。

### 存放位置与账号隔离

- 默认目录：`%APPDATA%\ChatBox\ChatBox\history\history.sqlite`（`AppDataLocation` + `history` 子目录，目录不存在会自动创建）。
- 可在「设置 → 本地聊天记录 → 更改目录」换成任意可写目录，「打开目录」直接打开该文件夹；设置页同时显示当前占用空间。
- 每个账号一个库：所有读写 SQL 都带 `user_id` 过滤，切换账号只看得到自己的记录，清理也只清当前账号。
- 换目录时可选择「一起迁移」或「只新建空库」：迁移会复制现有记录并校验行数；目标目录已存在 `history.sqlite` 时直接失败并继续使用旧目录。

### 写入规则

- **只保存服务端已确认的消息**（`message_id > 0` 且状态为已发送）。待发、发送中、失败的消息永远不落盘，本地库不会残留「其实没发出去」的记录。
- 落库时机：自己发送成功拿到 `message_id`、收到对方推送、打开会话时服务端返回的那一页历史。
- 按 `(user_id, 会话类型, 会话对象, message_id)` 去重，重复拉取同一页历史不会产生重复行。
- 在线时以服务端历史为准：重新打开会话会从服务端重新拉取并再次写入本地库。

### 离线模式

- 进入条件一：本机存有该账号的记录、但当前拿不到服务端（恢复会话失败或超时）→ 自动进入离线模式，顶部横幅显示「离线模式 · 仅显示本地记录」。
- 进入条件二：登录页出现「查看本地记录」按钮（本机用该账号登录过就会保留这个入口，退出登录也不清）→ 点击进入纯本地浏览：本机确实有记录才进得去，否则提示「本机没有该账号的聊天记录」；进入后不带 token、不算登录态、也不参与自动重连。
- 离线模式是**只读**的：输入框与发送按钮禁用，@ 提及、好友申请、我的资料、用户搜索、在线成员列表都不可用；整个离线过程不向服务端发送任何业务请求帧。
- 本地搜索：⋮ 菜单「消息搜索」只查**当前会话**的本地记录（最多 50 条），不发帧、不占用服务端序号；切到别的会话就是另一个搜索范围。
- 自动重连：离线模式仍保留 token 时按指数退避继续重连，横幅右侧出现「重连」按钮；重连成功即退出离线模式并恢复完整能力。纯本地浏览（无 token）不重连。
- 清理只删除本机副本：设置页「清理」支持「全部清理」和「清理 N 天前」，只影响当前账号的本地库，不动服务端数据；之后在线重新打开会话会从服务端重新拉取并再次落盘。

### 便携包要求

便携包必须带 `Qt5Sql.dll`、`sqldrivers\qsqlite.dll` 和 `imageformats\qjpeg.dll`（`package_client.ps1` 打包后逐项校验，缺任何一项直接报错、不产出 ZIP）。

缺少 SQLite 驱动时客户端仍能启动、可正常登录聊天，但本地历史整体降级为「不可用」：设置页「本地聊天记录」分组显示「本地历史不可用（缺少 SQLite 驱动）」、相关按钮置灰，登录页的「查看本地记录」入口点开后会提示「本机没有该账号的聊天记录」。缺 `imageformats\qjpeg.dll` 时只影响 JPEG 头像的编解码。

## 测试

客户端核心逻辑使用 QtTest：

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client\tests
.\run_client_tests.ps1
```

当前回归结果：76 passed、0 failed（含头像裁剪/压缩/回退、头像缓存版本、资料页按钮状态、昵称/签名字符计数、消息操作可用性、撤回窗口判定、气泡位置映射、悬停重入、重试链接样式、主题色板与 `style.qss` 一致性守卫、气泡配色来源、操作条单层阴影、`app.ico` 多帧尺寸守卫、大厅徽标配色、托盘图标来源守卫、在线成员列表头像来源守卫、字母头像调色板单一来源守卫、产品名品牌守卫、QSettings 迁移（拷贝/不覆盖）、统一标识符守卫、安装包模板 exe 名守卫、私聊无 @ 守卫、头部头像/⋮ 菜单守卫、系统弹窗开关守卫、开机自启动命令与 Run 项写入、单实例启动守卫、托盘可用性策略、未读角标开关守卫）。

本地历史与离线相关的覆盖（2026-10-20）：

- 历史库 schema 创建、打开失败上报、同一连接名的第二实例拒绝。
- 消息落库与最近消息读取、按 `message_id` 去重、未确认消息不落盘、撤回标记（正文清空）。
- 账号隔离与会话隔离、会话内本地搜索、会话摘要（最后一条消息与时间）。
- 清理全部 / 清理 N 天前、换目录迁移数据、目标目录已存在时拒绝并保留旧目录。
- 写穿透：服务端确认的消息才落库（`message_store` 挂接历史库后）。
- 离线只读：离线模式不发送任何业务帧、离线本地搜索只查当前会话。
- 设置页历史信息（存放位置、占用空间、清理/换目录/打开目录入口）展示。
- 登录页「查看本地记录」入口；构建脚本携带 SQLite 驱动的守卫。

## 已实现

- Coinbase 设计语言浅色 UI：白底、冷灰表面（`#F7F7F7` / `#EEF0F3`）、单一蓝色强调 `#0052FF`、hairline 分隔、4/8/12/16 圆角刻度（输入框与发送键保留 18px 胶囊）、操作条单层柔和阴影；己方气泡保留绿色语义（`#05B169` 的 10% tint `#E6F7F0`）。色板唯一来源 `core/ui_theme.h`，`themeTokensMatchStyleSheet` 测试守卫 `style.qss` 不漂移（规格：`../../docs/superpowers/specs/2026-10-19-coinbase-style-ui.md`）
- 品牌图标（2026-10-19）：应用/任务栏图标改为 `resources/app.ico` 多帧标记（16/24px 简化版纯气泡、32/48/64/128/256px 完整版蓝底白气泡 + 镂空 C；生成脚本 `tools/make_app_icon.ps1`，可重复再生成）；托盘图标同款（16px 简化帧）；大厅徽标改为品牌蓝气泡 `roomAvatar()`，房间列表与聊天头部共用，圆底取 `theme::kAccent`（规格：`../../docs/superpowers/specs/2026-10-19-client-brand-icons.md`、证据图 `screenshot-coinbase-icons.png`）
- 注册 + 注册成功自动登录
- 登录、Token 会话恢复
- TLS 加密连接（OpenSSL，证书校验）
- 断线指数退避重连、连接横幅、离线禁用发送
- 单大厅、在线成员
- 群聊发送/接收、本地回显
- 消息发送中/已发送/失败状态、10 秒超时、失败重发
- 聊天气泡、日期分隔、UTC 转本地时间
- 引用回复（悬停操作条 / 右键菜单）、@提及（仅房间：昵称识别并提醒；私聊不提供 @ 入口、不触发提及提醒）、2 分钟内撤回、消息搜索（⋮ 菜单）
- 私聊会话列表、私聊发送/接收
- 私聊未读数、任务栏闪烁、系统弹窗提醒（可在设置里关闭，关闭后只保留闪烁与角标）
- 房间/私聊历史分页（⋮ 菜单「加载更早消息」）
- Token 自动保存、启动 RESUME、退出登录
- 用户搜索（用户名/昵称/用户ID）、好友申请/接受/拒绝、好友列表、删除好友
- 个人主页：查看他人资料、编辑自己资料、头像上传/清除/回退、昵称/签名修改
- 在线好友资料推送（昵称 + 头像版本）、好友列表 `avatar_version`、头像按版本缓存
- 在线成员列表头像（2026-10-19）：成员行优先取头像缓存，未命中时按「版本未知先问一次」请求 `AVATAR_GET`，响应到达后重绘该行（含自己；证据图 `screenshot-member-avatar.png`）
- 服务端好友备注、在线/离线分组、两行消息预览、时间/未读角标
- 用户身份标识 `U + 8 位 user_id`，设置页可查看，搜索和好友申请中展示
- 全量改名统一（2026-10-19）：目录 `chatbox/`、可执行文件 `chatbox.exe`、C++ 命名空间 `chatbox::client`、头文件宏 `CHATBOX_*`、QSettings `ChatBox/ChatBox`、服务端目标 `chatbox_server`、systemd 服务 `chatbox`、MySQL 库/用户 `chatbox` 全部一致；启动时自动迁移旧 QSettings（`Chatroom/ChatroomClient` → `ChatBox/ChatBox`），用户无感
- 设置页：服务器、TLS、消息提醒、系统弹窗通知、开机自启动（静默到托盘）、日志目录
- 会话头部（2026-10-20）：只保留 `头像 + 标题 + ⋮`；点头像进入当前私聊对象资料（房间头像无动作），⋮ 菜单含「消息搜索」与房间的「加载更早消息」，输入区不再有搜索按钮
- 启动行为（2026-10-20）：单实例锁（第二次启动提示后退出）；托盘菜单「打开 ChatBox / 退出」；`--minimized` 启动只驻留托盘，托盘不可用时回退为正常窗口；开机自启动写入 `HKCU\...\Run\ChatBox`
- 本地聊天记录库（2026-10-20）：SQLite `history.sqlite`，默认 `%APPDATA%\ChatBox\ChatBox\history\`，按账号隔离（`user_id` 过滤）；只落服务端已确认的消息，写穿透 + `message_id` 去重 + 会话摘要 + 撤回同步
- 离线只读模式（2026-10-20）：恢复会话失败/超时自动进入离线模式（横幅「离线模式 · 仅显示本地记录」）；登录页「查看本地记录」纯本地浏览（无 token、不重连）；离线期间不发送任何业务请求帧；⋮ 菜单本地搜索只查当前会话；保留 token 时按指数退避自动重连
- 设置页「本地聊天记录」分组（2026-10-20）：存放位置、占用空间、清理（全部 / 清理 N 天前）、更改目录（可迁移 / 只建空库）、打开目录
- 便携包携带 `Qt5Sql.dll` 与 `sqldrivers\qsqlite.dll`（2026-10-20）：`package_client.ps1` 打包后逐项校验 `chatbox.exe`、`Qt5Sql.dll`、`sqldrivers\qsqlite.dll`、`imageformats\qjpeg.dll`，缺项直接报错不产出 ZIP
- 客户端日志文件、图标、版本信息和便携 ZIP
- 自己的推送按 `sender_id` 忽略，避免与本地回显重复

## 说明

- 网络线程用原生 Winsock，UI 不直接操作 socket。
- Token 保存在 `QSettings`，启动时自动 RESUME；退出登录只清除会话，好友和设置保留。
- 好友关系保存在服务端 MySQL，搜索用户后发送申请，对方接受后进入好友列表。
- 好友备注按当前账号保存在服务端，换电脑或重装客户端后仍可见；好友本人看不到该备注。
- 用户身份标识由不可变数据库主键生成，格式为 `U00000278`，不是可修改的昵称。
- 用户名 `@username` 只用于登录且不可修改；昵称用于展示，可以修改，二者不是同一个字段。
- 头像保存在服务端 `users.avatar`（最大 48 KiB），按 `avatar_version` 缓存；清除头像不会删除历史版本号，而是继续递增并回退首字母。
- 成员列表的成员多数不是好友，其头像版本不出现在好友/会话列表里：客户端按「版本未知先问一次」的策略请求一次 `AVATAR_GET`（服务端返回当前版本，含"无头像"），并把结果写进同一头像缓存；因此成员换头像后的展示依赖下次成员列表加载或资料更新推送。
- 字母头像（无头像时的回退）统一取色于 `core/avatar_utils.cpp::fallbackAvatar()`；修复前 `main_window.cpp` 自带 6 色私有表、而资料页用 8 色表，同一用户两处底色不同（证据图 `screenshot-avatar-palette.png`）。
- JPEG 头像运行时必须有 `imageformats\qjpeg.dll`；该文件由 `build_client.ps1` 复制到构建输出。
- 窗口/托盘图标运行时读取 `resources/app.ico`，需要 `imageformats\qico.dll`（`build_client.ps1` 一并复制）；缺少该插件时窗口图标回退为 exe 内嵌图标（`app.rc` 的 `IDI_ICON1`），托盘图标可能不显示。
- 设置页修改服务器/TLS 会结束当前会话，返回登录页并使用新地址登录。
- 本地历史只是服务端数据的**本机副本**：在线时以服务端历史为准，重新打开会话会从服务端重新拉取并再次落盘；待发/失败消息不落盘。
- 离线模式是只读视图：输入与发送、@ 提及、好友申请、我的资料、用户搜索、在线成员列表均不可用，离线期间除重连外不产生任何业务请求。
- 清理只删本机副本，不动服务端数据；按 `user_id` 隔离，只清当前账号。
- 切换存储目录在设置页发起：迁移会复制现有记录并校验行数，目标目录已存在 `history.sqlite` 时直接失败并继续使用旧目录。
- 缺少 `Qt5Sql.dll` / `sqldrivers\qsqlite.dll` 时本地历史整体降级为「不可用」（设置页显示「本地历史不可用（缺少 SQLite 驱动）」），聊天功能不受影响。
- 日志：`%APPDATA%\ChatBox\ChatBox\logs\client.log`，超过 5 MB 轮转为 `.1`。
- Outbox 只保存在进程内：应用退出后，未发送/失败消息会丢失；跨进程持久化尚未实现。
- 当前仍没有聊天图片/文件传输（头像除外）、群管理。
- @提及目前按昵称匹配并触发客户端提醒，不保存服务端 mention 表。
- 消息搜索返回结果列表，暂不支持双击定位到历史位置。
- 参考截图：`screenshot-profile-own.png`、`screenshot-profile-friend.png`、
  `screenshot-profile-edit-avatar.png`、`screenshot-profile-saved.png`、
  `screenshot-profile-friend-avatar.png`、`screenshot-profile-friend-push.png`、
  `screenshot-profile-fallback.png`、`screenshot-profile-chat-header.png`、
  `screenshot-final-server-remark.png`、`screenshot-friend-menu2.png`、
  `screenshot-final-friends-id.png`、`screenshot-server-remark-ui.png`、
  `screenshot-friends-two-line.png`、`screenshot-friends-final2.png`、
  `screenshot-friend-request.png`、`screenshot-friends-final.png`、
  `screenshot-telegram-final3.png`、`screenshot-telegram-login2.png`、
  `screenshot-p1-final2.png`、`screenshot-search-result.png`、
  `screenshot-contact-added.png`、`screenshot-settings.png`、
  `screenshot-quote-bar.png`、`screenshot-quote-sent.png`、
  `screenshot-recall-done.png`、`screenshot-msg-search-result.png`、
  `screenshot-message-actions-hover-own.png`、`screenshot-message-actions-hover-incoming.png`、
  `screenshot-message-actions-no-recall.png`、`screenshot-message-actions-menu-own.png`、
  `screenshot-message-actions-menu-incoming.png`；
  Coinbase 换肤版（2026-10-19）：`screenshot-coinbase-login.png`、`screenshot-coinbase-main.png`、
  `screenshot-coinbase-message-actions.png`、`screenshot-coinbase-message-menu.png`、
  `screenshot-coinbase-profile.png`、`screenshot-coinbase-settings.png`、
  `screenshot-coinbase-message-search.png`、`screenshot-coinbase-user-search.png`、
  `screenshot-coinbase-friend-request.png`；
  品牌图标证据（2026-10-19）：`screenshot-coinbase-icons.png`（多帧 app.ico 阶梯、任务栏按钮、托盘图标、大厅徽标）；
  在线成员列表头像修复（2026-10-19）：`screenshot-member-avatar.png`（修复前字母头像 / 修复后照片）；
  字母头像调色板统一（2026-10-19）：`screenshot-avatar-palette.png`（三处底色统一 + 像素级验证）；
  全量改名统一（2026-10-19）：`screenshot-chatbox-rename.png`（exe 为 `chatbox.exe`、窗口标题 ChatBox、会话经 QSettings 迁移后自动登录成功）。
