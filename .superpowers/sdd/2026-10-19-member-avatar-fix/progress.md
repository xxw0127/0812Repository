# 在线成员列表头像修复 — 执行台账

日期：2026-10-19
触发：图标工作收尾后报「换了头像但在线成员不显示照片」（证据：成员「武政」为紫色字母头像，而同窗口左下角自己已是照片）。
方法：systematic-debugging（Phase 1 根因取证 → Phase 2 与既有链路对照 → Phase 3 最小修复 → Phase 4 验证）。

## Phase 1 根因（证据）

`ui/main_window.cpp` 的 `kCmdRoomMembers` 处理块（约 L3010-3035）在服务端成员列表响应后直接
`item->setIcon(avatarIcon(nickname, member_id))` —— 永远画字母头像：

- 不查 `avatar_cache_`（其它身份列表都用同一缓存）；
- 不调用 `requestAvatar()` / `requestPeerAvatar()`，从不请求头像；
- 服务端 `kCmdRoomMembers` 载荷只有 `(room_id, count, [user_id, nickname]×N)`
  （`server/room/room_service.cpp:146-173`），不含 `avatar_version`。

## Phase 2 对照（既有可用链路）

- 好友列表 / 私聊头部等都有完整链路（`avatar_version` → `requestAvatar` → 缓存 → 重绘）。
- 客户端对「没有任何列表版本号的非好友」已有专用通道 `requestPeerAvatar()`
  （`main_window.cpp:911-928`，注释原文：a non-friend has no version in any list yet: ask once with
  version 0 and cache the version returned by the server, including "no avatar"）。
- 服务端 `handleAvatarGet`（`server/profile/profile_service.cpp:112-140`）请求只需 `user_id`，
  响应 `(user_id, version, avatar)`；无头像时 version 0 + 空串。正好适配成员列表（成员多为非好友）。

结论：缺的是**接线**，不是新机制。修复放客户端，不改协议 / 服务端 / DB（对现网服务端立即可用）。

## Phase 3 修复（TDD）

红灯：新增源扫描守卫测试 `memberListShowsAvatarFromCache`（照 `trayIconUsesBrandIcon` 模式）。
首次运行：`38 passed, 1 failed`（第一条断言失败，符合预期）。

改动（`ui/main_window.h` / `ui/main_window.cpp`）：

1. 新私有方法 `QIcon memberAvatarIcon(member_id, nickname, size)`：`avatar_cache_.lookup()` 命中且非空
   → `circularAvatar`；否则字母兜底（昵称空则取 `publicUserId`）。
2. 新私有方法 `void refreshMemberListAvatar(user_id)`：按 `Qt::UserRole` 定位成员行重设图标（28px，与 `setIconSize(QSize(28,28))` 一致）。
3. `kCmdRoomMembers` 填充循环：`item->setIcon(memberAvatarIcon(...))`；
   自己 → `!avatar_cache_.contains()` 时 `requestAvatar(self, 0)`（`requestPeerAvatar` 跳过自己）；
   其它成员 → `requestPeerAvatar(member_id)`。
4. `onProfileAvatarReceived` / `onProfileDataReceived`：缓存写入后 `refreshMemberListAvatar(user_id)`（头像到达即重绘）。
5. `updateRoomMemberNickname`（昵称推送 / 自己改名）：`setText` 后按新昵称一并重设图标。

绿灯：`39 passed, 0 failed`；客户端 release 构建通过。

## Phase 4 验证

- 测试：`tests/run_client_tests.ps1` → **39 passed / 0 failed**。
- 构建：`build_client.ps1` → `build/release/chatroom_client.exe`（main_window.cpp 实编，非仅测试）。
- 实机：新构建启动（日志 `19:45:34 resume ok user_id=483`、`profile loaded user_id=483 version=1`），
  打开大厅 → **在线成员「武政」显示照片**（修复前为紫色字母）。
- 证据图：`chatroom/client/screenshot-member-avatar.png`（修复前/后对照 + 两个主窗口）。
- README：回归数字 38→39、「已实现」新增条目、「说明」新增成员列表头像语义、截图清单追加。

## 已知限制 / 备注

- 成员为非好友且别处未缓存时，成员列表加载会为每人发一次 `AVATAR_GET`（现网大厅 1-2 人，可接受）；
  房间规模变大后正解是服务端在 `kCmdRoomMembers` 携带 `avatar_version`（协议扩展 + 服务端发版），本轮刻意不做。
- 成员在列表打开期间换头像：是好友则有资料推送（既有链路 + 新重绘）；非好友无推送，
  依赖下次成员列表加载（根源同样是载荷无版本号）。
- 本轮实机验证的账号在大厅里只有自己在线；「非好友成员」路径复用既有 `requestPeerAvatar`
  （与此前私聊非好友头像验证同一通道）。

## 追加修复（同日）：字母头像调色板不一致

反馈：同一用户回退头像的底色不一致（侧边栏 / 在线成员列表为紫，资料页为橙红）。

根因：调色板有两份 ——

- `ui/main_window.cpp` 私有 `avatarIcon()`：6 色表 `{kAccent, #4CAF50, #F5A623, #9B59B6, #E74C3C, #16A085}`，`seed % 6`；
  483 % 6 = 3 → `#9B59B6`（紫）。
- `core/avatar_utils.cpp::fallbackAvatar()`：8 色表 `{kAccent, #5AD8A6, #F6BD16, #E8684A, #6DC8EC, #9270CA, #FF9D4D, #269A99}`，`seed % 8`；
  483 % 8 = 3 → `#E8684A`（橙红）；资料页用它。

像素级取证（全屏扫描；窗口在 (392,129) 1136x759）：

- 修复前：`#9B59B6` = 1007 px（侧边栏 + 成员列表），`#E8684A` = 6334 px（资料页大头像）。
- 修复后：`#9B59B6` = 0 px，`#E8684A` = 7280 px（三处 + 资料页）。

修复（TDD）：新增守卫测试 `fallbackAvatarPaletteIsSingleSource`（禁私有表 hex、要求调用
`chatroom::client::fallbackAvatar(`）→ 先红（39 passed / 1 failed）→
`avatarIcon()` 改为 `return QIcon(QPixmap::fromImage(chatroom::client::fallbackAvatar(name, seed, 36)));`
→ 绿 **40 passed / 0 failed** → release 构建 → 实机复验（扫描数字如上）。

副作用：好友列表 / 会话列表 / 私聊头部等所有回退头像底色随统一后的 8 色表变化（与资料页一致，
符合「同一用户同一色」目标）。保留：`avatarIcon("C", 0)` 占位仍为品牌蓝 `theme::kAccent`（8 色表第 0 项）。

## 备注：QTrayIconMessageWindow 误显示（已恢复）

取证脚本曾对「宽度 > 800 的窗口」统一 `SWP_SHOWWINDOW`，把 Qt 托盘气泡用的隐藏辅助窗口
（类名 `Qt5152TrayIconMessageWindowClass`）显示成白色空面板；已 `ShowWindow(SW_HIDE)` 恢复，
后续脚本不再使用 `SWP_SHOWWINDOW`（并修正 `GetWindowTextW` 缺 Unicode 字符集导致的标题误读）。
