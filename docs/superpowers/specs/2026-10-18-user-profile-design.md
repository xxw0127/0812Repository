# 用户个人主页与头像上传设计

**日期：** 2026-10-18  
**状态：** 待确认  
**范围：** 查看他人资料、编辑自己的资料、上传头像、昵称与用户名分离

## 目标

给客户端增加类似 QQ/微信的个人主页：

- 从好友列表头像、好友右键菜单、群成员菜单、聊天顶部资料入口打开他人主页。
- 左侧栏底部增加自己的资料入口。
- 主页展示大头像、昵称、用户名、UID、个性签名、在线状态和好友关系。
- 自己的主页可以修改昵称、个性签名、上传或移除头像。
- 用户名只用于登录，不可修改；昵称只用于展示，可以修改。
- 没有上传头像时，继续使用彩色首字母头像。
- 好友在线时，资料变更通过一条轻量推送同步昵称和头像版本。

## 非目标

- 本轮不展示 IP；以后需要时再加字段和 UI。
- 不支持修改用户名。
- 昵称不要求全局唯一。
- 不做性别、地区、生日、手机号等扩展资料。
- 不做全量资料广播；只向在线好友推送昵称和头像版本，签名和头像字节由客户端按需拉取。
- 不做图片裁剪框的拖拽/缩放；客户端自动居中裁成正方形。
- 群成员列表、搜索结果暂不批量显示真实头像，继续显示首字母头像；打开主页或私聊后按需拉取。
- 不做隐私开关；任何已登录用户都可以按用户 ID 查看公开资料。

## 资料字段

| 字段 | 来源 | 可编辑 | 说明 |
|---|---|---|---|
| UID | `users.id` | 否 | 展示为 `U00000278`，全局唯一、不可变 |
| 用户名 | `users.username` | 否 | 登录标识；资料页显示为 `@username` |
| 昵称 | `users.nickname` | 是 | 页面展示名；1–32 个 Unicode 字符，去除首尾空白 |
| 个性签名 | `users.signature` | 是 | 0–128 个 Unicode 字符，可为空 |
| 头像 | `users.avatar` | 是 | JPEG/PNG，最大 48 KiB；空则首字母头像 |
| 头像版本 | `users.avatar_version` | 服务端维护 | u32，每次上传/清除递增，用于客户端缓存失效 |
| 备注 | `friend_remarks.remark` | 是 | 仅资料查看者可见；好友主页优先显示备注 |
| 在线状态 | SessionManager | 否 | 在线/离线 |
| 好友关系 | `friendships` / `friend_requests` | 否 | 自己、好友、无关系、已发申请、收到申请 |

### 字符计数规则

昵称和签名统一按 Unicode code point 计数，中文、英文、Emoji 的基础码点都算 1 个字符。服务端和客户端必须使用同一规则：

- 服务端新增 UTF-8 code point 计数器，替换现有 `std::string::size()` 字节判断。
- 客户端使用 `QString::toUcs4().size()`，不用 UTF-16 的 `QString::size()`。
- 现有昵称读取缓冲区从 33 字节扩大到 129 字节（32 个 UTF-8 字符最多 128 字节 + 结尾）。
- 历史消息、私聊会话、好友列表、用户搜索等所有读取 `nickname` 的位置同步扩大缓冲区。
- `signature` 读取缓冲区按 128 字符 × 4 字节 + 1 准备。
- 这项修复属于本轮范围，避免 32 个中文字符的昵称在登录、搜索、好友列表或历史消息中被截断。

## 客户端页面

### 自己的资料入口

左栏底部增加一条个人资料栏：

- 32px 头像（真实头像或首字母）
- 昵称
- `U00000278`
- 点击整条打开自己的主页

### 他人主页

入口：

- 好友列表点击头像区域。点击好友行其他区域仍然打开私聊。
- 好友右键菜单「查看资料」。
- 群成员右键菜单「查看资料」。
- 私聊聊天顶部增加「资料」按钮。
- 用户搜索结果对话框增加「查看资料」按钮。
- 好友申请对话框双击申请人的昵称或增加「查看资料」按钮。

布局：

- 顶部大头像 96px，圆形裁切；离线/在线状态点。
- 昵称大字显示。
- `@username` 和 `U00000278` 分两行，可选择复制。
- 个性签名，空时显示「这个人很懒，什么都没写」。
- 底部操作按钮：
  - 自己：`编辑资料`
  - 好友：`发消息`、`设置备注`、`删除好友`
  - 无关系：`添加好友`
  - 已发申请：按钮禁用，显示「申请已发送」
  - 收到申请：`查看好友申请`

### 编辑自己的资料

- 头像预览 +「更换头像」/「移除头像」
- 昵称输入框，1–32 字符
- 个性签名多行输入，0–128 字符
- 用户名和 UID 只读展示
- 「保存」
- 保存成功后更新本地 `nickname_`、会话 QSettings、左栏资料栏和资料页；不覆盖当前私聊/房间标题。
- 保存失败保留输入并显示服务端错误。

## 头像上传

### 客户端处理

1. 使用 `QFileDialog` 选择 PNG/JPEG。
2. 用 `QImageReader` 读取，居中裁成正方形。
3. 缩放到 256×256。
4. 编码为 JPEG quality 85。
5. 如果结果大于 48 KiB，依次降低 quality 到 75、65，并把尺寸降到 192×192、128×128，直到不超过 48 KiB。
6. 仍超限则提示「图片压缩后仍然过大」。
7. 上传前可在编辑页预览。
8. 移除头像发送 `avatar_action=clear`，回到首字母头像。

### 服务端校验

- 只接受 JPEG（`FF D8 FF`）或 PNG（`89 50 4E 47`）文件头。
- 空数据视为清除。
- 最大 48 × 1024 字节。
- 存储原始字节到 `users.avatar`，不依赖服务端图像库。
- 上传/清除时 `avatar_version = avatar_version + 1`。
- 昵称和签名在同一次更新中保存。

## 协议

新增命令：

```text
0x00A0 PROFILE_GET
请求：target_id u64
响应：target_id u64,
      username string,
      nickname string,
      signature string,
      avatar_version u32,
      avatar bytes string,
      online u8,
      relationship u8,
      remark string

0x00A1 PROFILE_UPDATE
请求：nickname string,
      signature string,
      avatar_action u8,  // 0 不变，1 设置，2 清除
      avatar bytes string // 仅 action=1 时存在
响应：nickname string,
      signature string,
      avatar_version u32

0x00A2 AVATAR_GET
请求：target_id u64
响应：target_id u64,
      avatar_version u32,
      avatar bytes string

0x00A3 PROFILE_UPDATE_PUSH
推送：user_id u64,
      nickname string,
      avatar_version u32
```

`relationship`：

```text
0 = 自己
1 = 好友
2 = 无关系
3 = 我已发送好友申请
4 = 对方已向我发送好友申请
```

协议字符串沿用 `u16 长度 + UTF-8/二进制字节`。头像最大 48 KiB，完整 `PROFILE_UPDATE` 请求仍在客户端 64 KiB 帧上限内。

`FRIEND_LIST` 响应增加 `avatar_version u32`：

```text
count u16 + [user_id u64, username, nickname, online u8, remark, avatar_version u32]
```

## 数据库

新增迁移 `deploy/migrate_profiles.sql`：

```sql
ALTER TABLE users
    ADD COLUMN signature VARCHAR(128) NOT NULL DEFAULT '' AFTER nickname,
    ADD COLUMN avatar MEDIUMBLOB NULL AFTER signature,
    ADD COLUMN avatar_version INT UNSIGNED NOT NULL DEFAULT 0 AFTER avatar;
```

说明：

- 不修改 `username`，它继续作为登录键。
- `nickname` 仍不唯一。
- 头像放 `MEDIUMBLOB`，按 48 KiB 限制，避免单独文件服务。
- 现有用户默认无头像、空签名、版本 0。

## 服务端

新增 `ProfileService`，每个业务 worker 使用自己的 MySQL 连接。

### PROFILE_GET

1. 校验 `target_id != 0`。
2. 查 `users` 的 id、username、nickname、signature、avatar_version、avatar。
3. 查好友关系：
   - 自己
   - 好友
   - 无关系
   - 待处理申请方向
4. 用 `SessionManager` 判断在线。
5. 返回资料。

### PROFILE_UPDATE

只允许修改调用者自己的资料：

1. 昵称去首尾空白，1–32 个 Unicode 字符。
2. 签名去首尾空白，0–128 个 Unicode 字符。
3. 校验头像动作、格式、大小。
4. 更新数据库；头像上传或清除时 `avatar_version = avatar_version + 1`。
5. 返回新昵称、签名、头像版本。
6. 昵称变化时：
   - 通过当前连接所属 IO 线程投递 `Connection::setNickname(new_nickname)`。
   - 调用 `RoomManager::updateNickname(user_id, new_nickname)` 更新房间成员快照。
   - 当前连接后续群聊/私聊推送使用新昵称。
7. 在线好友收到 `PROFILE_UPDATE_PUSH`：
   - 客户端更新好友列表中的昵称和头像版本。
   - 如果版本变化，发送 `AVATAR_GET` 拉取新头像。
   - 签名不放入推送，好友打开资料页时再拉取。
8. 资料推送失败不影响保存成功；服务端记录日志并继续。

### AVATAR_GET

1. 查 `users.avatar, avatar_version`。
2. **版本和字节必须来自同一条记录**：即使头像为空，也返回数据库中保存的 `avatar_version`，而不是强制返回 0。
3. 头像为空时返回空字节 + 当前版本；客户端按该版本缓存“无头像”，不会重复请求。
4. 客户端按版本缓存，不重复请求。
5. 私聊打开非好友时也允许调用 `AVATAR_GET`，返回空则回退首字母。

### 路由

- `0x00A0–0x00A3` 归入 `profile_command`。
- 主线程把资料命令投递给 worker。
- `ProfileUpdateResult` 带 `nickname_changed`、`new_nickname`、`avatar_version` 和在线好友列表。
- 主线程负责：
  - 更新连接昵称（投递到连接所属 IO 线程）。
  - 向在线好友 IO 线程投递 `PROFILE_UPDATE_PUSH`。
- 更新房间成员昵称在 worker 内调用线程安全的 `RoomManager`。

### 昵称更新后的显示范围

自己的昵称修改后，当前客户端立即更新：

- 左栏自己的资料栏
- 自己的资料页标题/预览
- 会话 QSettings 的 `session/nickname`
- 后续自己发送的房间/私聊消息的 `sender_name`

不修改：

- 私聊聊天顶部（那里显示对方的昵称/备注）
- 房间聊天顶部（显示房间名）
- 主窗口标题（只显示未读数）

其他在线好友：

- 收到 `PROFILE_UPDATE_PUSH` 后更新好友列表名称。
- 如果备注非空，列表仍显示备注，不显示新昵称。
- 聊天顶部若显示的是备注，也保持不变；打开资料页可看到新昵称。

## 客户端实现

### 新增文件

```text
client/core/avatar_utils.h/.cpp      图片居中裁剪、缩放、压缩、首字母头像（QImage）
client/core/avatar_cache.h/.cpp      按 user_id + version 缓存 QImage
client/ui/profile_dialog.h/.cpp      自己/他人资料页
client/ui/profile_edit_widget?       可并入 ProfileDialog
client/ui/friend_list_widget.h/.cpp  识别好友头像区域点击（可选，事件过滤器也可）
```

### 头像缓存

- `AvatarCache` 键为 `user_id`，值为 `version + QImage + loaded`；UI 层再把 `QImage` 转成 `QPixmap`。
- 收到 `FRIEND_LIST` 后，对每个 `avatar_version != 0` 且缓存版本不一致的好友发送 `AVATAR_GET`。
- 用 `QSet<quint64> pending_avatar_requests_` 防止重复请求。
- 收到头像后更新缓存，刷新好友列表、聊天顶部和资料页。
- 无头像或解码失败：使用彩色首字母头像。
- 好友列表 delegate 增加 `QPixmap` 数据角色；有真实头像时绘制圆形头像，否则绘制首字母。
- 群成员/搜索结果本轮不批量请求头像，继续首字母。
- 打开非好友私聊时发送一次 `AVATAR_GET`；缓存版本后，聊天顶部显示真实头像或首字母。
- `PROFILE_UPDATE_PUSH` 中头像版本变化时，清除旧缓存并重新请求；版本不变只更新昵称。

### 备注显示

- `PROFILE_GET` 返回查看者对该好友的备注（非好友为空）。
- 资料页主标题优先显示备注，下方显示服务器昵称。
- 「发消息」传入 `selectPrivate` 的名称与好友列表一致：有备注传备注，否则传昵称。
- 没有备注时主标题显示昵称。

### 头像显示位置

- 好友列表行头像
- 私聊聊天顶部头像
- 左栏自己的资料栏
- 个人主页大头像
- 设置页「我的ID」保持不变；可增加头像预览但不强制

### 好友头像点击

使用自定义 `FriendListWidget` 或事件过滤器：

- 左键释放时取得 `itemAt(pos)`。
- 如果点击横坐标位于该行头像区域（约行左 + 4 到 + 58），打开资料页，不触发私聊切换。
- 其他区域保持原有行为，打开私聊。
- 群成员列表双击仍然打开私聊；右键菜单提供「查看资料」。
- 私聊聊天顶部「资料」按钮打开当前对方资料。
- 「发消息」使用备注/昵称规则，与好友列表一致。
- 资料页关闭后不改变当前聊天上下文。

### 资料页交互

- `ProfileDialog` 支持两种模式：view / self-edit。
- 打开他人资料时发送 `PROFILE_GET`。
- 打开自己资料时从 `PROFILE_UPDATE` 前的本地数据或 `PROFILE_GET` 获取；保存后更新本地。
- 「发消息」调用主窗口 `selectPrivate`，关闭资料页。
- 「设置备注」复用现有 `setFriendRemark`。
- 「删除好友」复用现有删除流程，确认后关闭资料页并刷新好友列表。
- 「添加好友」发送现有 `FRIEND_REQUEST`；成功后按钮变「申请已发送」。
- 收到申请状态时打开现有好友申请对话框。

## 错误码

```text
1001 参数错误（昵称/签名/头像动作非法）
1008 用户不存在
1017 头像格式不支持或超过 48 KiB
2001 服务端内部错误
```

## 测试与验收

### 服务端集成 `tools/profile_client.py`

- 注册两个用户并登录。
- 查看他人资料：用户名、昵称、UID、签名、在线、关系正确。
- 修改昵称和签名，再次 GET 生效。
- 用户名不可通过 PROFILE_UPDATE 修改。
- 上传 1×1 或小 JPEG，`avatar_version` 增加，AVATAR_GET 字节一致。
- 清除头像，版本增加，AVATAR_GET 返回空字节 + 同一个新版本，不回到 0。
- 非 JPEG/PNG 文件头被拒绝 1017。
- 超过 48 KiB 被拒绝 1017。
- 空昵称被拒绝 1001。
- 非好友查看资料允许。
- 好友查看资料时返回备注；非好友返回空备注。
- 更新昵称后，新群聊/私聊推送使用新昵称。
- 在线好友收到 `PROFILE_UPDATE_PUSH`，离线好友不收到。
- 好友列表包含 avatar_version。
- 更新 `friend_client.py` 解析新字段并回归。

### 客户端测试

- `AvatarUtils` 居中裁剪、缩放、压缩后尺寸 ≤ 48 KiB。
- 无头像时首字母回退。
- `AvatarCache` 版本变化后重新请求。
- 昵称长度/签名字符数校验。
- 资料页自己/好友/陌生人按钮状态。
- QtTest 数量更新。

### 手工验收

- 左栏自己资料栏打开自己的主页。
- 好友头像点击打开对方主页，好友行其他区域打开私聊。
- 群成员右键菜单打开资料。
- 上传头像后好友列表、聊天顶部、资料页显示同一头像。
- 移除头像后回退首字母。
- 修改昵称后自己的资料栏、资料页、后续发送消息使用新昵称；私聊对方标题不被覆盖。
- 在线好友列表收到新昵称/头像；有备注时仍显示备注。
- 用户名和 UID 只读。
- 非好友私聊顶部也能加载真实头像。
- 重启客户端后头像、昵称、签名仍存在。

## 兼容与迁移

- 旧客户端不认识 `0xA0–0xA3`，新服务端不影响旧功能。
- `FRIEND_LIST` 增加字段属于不兼容变更；客户端与服务端同步升级。
- 生产执行 `deploy/migrate_profiles.sql` 后重启 `chatroom`。
- 迁移脚本按一次性执行设计；部署文档记录执行状态，避免重复 ALTER 报错。

## 已定决策

- 昵称、签名按 Unicode code point 计数，并修复现有昵称读取的 32 字节截断。
- 头像版本只增不减；清除头像也递增，AVATAR_GET 返回当前版本 + 空字节。
- 在线好友实时收到昵称/头像版本推送；离线好友下次登录拉取。
- 任何已登录用户都可以查看他人公开资料。
- 头像自动居中裁剪，不做交互式裁剪框。
- 群成员列表和搜索结果暂不批量显示真实头像；打开资料/私聊时按需拉取。
- 好友备注只影响查看者；资料页优先显示备注。
- IP 字段留到以后，本轮不预埋 UI。

## 实现结果（Task 11，2026-10-19）

状态：已实现并部署到生产环境。本工作区不是 Git 仓库，因此没有提交记录，验收以源码快照、测试输出和部署检查为准。

### 协议与服务端

- 已实现 `0x00A0 PROFILE_GET`、`0x00A1 PROFILE_UPDATE`、`0x00A2 AVATAR_GET`、`0x00A3 PROFILE_UPDATE_PUSH`；`FRIEND_LIST` 响应已包含末尾 `avatar_version u32`。
- `PROFILE_UPDATE` 支持昵称/签名/头像动作 0/1/2，服务端只允许修改调用者自己的资料；头像仅接受 JPEG/PNG 文件头且不超过 48 KiB，上传或清除都会递增 `avatar_version`。
- `AVATAR_GET` 的版本和字节来自同一条记录；空头像返回空字节和当前版本。
- 昵称变化会更新连接昵称和房间成员快照；在线好友收到 `PROFILE_UPDATE_PUSH`，离线好友不推送。
- 昵称和签名按 Unicode code point 计数；昵称 1–32、签名 0–128，相关读取缓冲区已扩大，32 个中文字符昵称不再截断。

### 数据库

- `deploy/migrate_profiles.sql` 已执行，`users` 表包含 `signature VARCHAR(128)`、`avatar MEDIUMBLOB`、`avatar_version INT UNSIGNED`；脚本按列存在性判断，可重复执行。
- 生产库既有用户保持空签名、无头像、版本 0，没有删除或回滚任何数据。

### 客户端

- 新增个人主页（自己/他人）、编辑昵称和签名、头像选择/预览/上传/移除、首字母回退、按 `user_id + version` 的头像缓存。
- 入口覆盖：左栏自己的资料栏、好友列表头像点击和右键、群成员右键、私聊顶部「资料」、用户搜索结果、好友申请对话框。
- 上传头像使用居中裁剪、256×256、JPEG quality 85；超过 48 KiB 时降到 quality 75/65 和 192×192、128×128，仍超限提示失败。
- `client/build_client.ps1` 现在会创建 `build\release\imageformats` 并复制 Qt `qjpeg.dll`，保证 JPEG 头像在构建输出中可复现。未修改 `package_client.ps1` 或分发/安装逻辑。
- 在线好友推送会更新好友列表昵称和头像；有备注时列表和聊天顶部仍优先显示备注；自己的昵称更新不会覆盖当前私聊/房间标题。

### 验证结果

- 服务端构建通过，CTest 5/5（`protocol`、`text_utils`、`buffer`、`metrics`、`auth`）。
- 客户端构建通过，QtTest 27 passed / 0 failed。
- 临时 9102 明文实例：`profile_client.py` 全部通过（资料、关系、昵称/签名、32 中文字符昵称、头像上传/清除版本、1017、在线推送/离线不推送）；`friend_client.py` 全部通过；`room_client.py` 全部通过；测试后 9102 已停止。
- 生产部署：`chatroom`、`nginx`、`mysql` 均为 active；`9100` 只监听 `127.0.0.1`；`9443` 的 `tls_smoke.py` 通过（TLS 登录和房间列表）。
- 当前客户端连接 `192.168.2.113:9443` 自动 RESUME 成功；服务端重启后客户端在约 2 秒内重连并再次 RESUME 成功。
- 手工 UI 验收：自己的资料页从底部资料栏打开；好友头像点击打开好友主页并显示真实头像；头像上传、保存、清除和首字母回退；在线好友昵称/头像推送；私聊顶部显示真实头像；用户名和 UID 只读。
- 截图见 `client/screenshot-profile-*.png`（own、friend、edit-avatar、saved、friend-avatar、friend-push、fallback、chat-header）。

### 已知限制

- IP 展示仍为后续工作，本轮未增加字段或 UI。
- 服务端 1017 头像格式/大小错误在 GUI 正常选择图片时通常不可达（客户端会先压缩到 48 KiB 内），但错误码映射和通用错误展示已实现。
- 协议没有昵称版本号，极端的“旧 `PROFILE_GET` 响应覆盖刚收到的昵称推送”竞态理论上仍可能存在；头像用版本号保护。该限制不影响本轮验收。
- 仅完成构建输出中的 Qt imageformats 复制；便携 ZIP/安装包未重新生成（分发工作已暂停）。
