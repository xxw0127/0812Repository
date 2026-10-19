# SDD ledger — plan: docs/superpowers/plans/2026-10-19-coinbase-style-ui.md

Setup: 工作区不是 git 仓库 → 无 worktree、无 commit 步骤；executing-plans 的 task-start/task-done 脚本依赖 git，改为人工执行 brief + 直接运行 `tests/run_client_tests.ps1` 并把结果记入本文件。
Setup: Ruling: 无 git 环境下用本文件当台账，任务完成条件 = 测试命令输出 + 构建 + 截图证据 — 原因：用户在无仓库工作区工作 — 代价：失败时无法用 git 回滚，只能靠证据回溯。

Pre-flight: 共享接口 — Task 1 产出 `core/ui_theme.h`（theme:: 常量）与 `style.qss` 新色板；Task 2（渲染器）、Task 3（10 文件）、Task 4（阴影）消费同一批常量；Task 1 自带守卫测试消费 QSS。核对计划中引用的常量名与 Task 1 头文件清单一致（kAccent/kAccentHover/kAccentActive/kAccentDisabled/kCanvas/kSurfaceSoft/kSurfaceStrong/kSurfaceStrongHover/kHairline/kInk/kBody/kMuted/kMutedSoft/kOnPrimary/kSemanticUp/kSemanticDown/kBubbleOwn/kBubbleOther/kBannerErrorBg/kBannerErrorLine）→ 无冲突。
Pre-flight: Task 3 更新 `chatViewStylesRetryLink` 期望值（chat_view 自身的 kLinkColor），与 Task 2 的渲染器改动互不共享接口 → 无冲突。

## 任务进度

Task 1: complete (tests: tests/run_client_tests.ps1 → 33/33 pass; RED 先行: themeTokensMatchStyleSheet 因 QSS 缺 #0052FF 失败; build_client.ps1 成功; 截图 screenshot-coinbase-main.png 确认 QSS 色板生效——蓝色发送键/浅灰表面/hairline; 渲染器气泡此时仍为旧色，按计划留在 Task 2)
Task 2: complete (tests: tests/run_client_tests.ps1 → 34/34 pass; RED 先行: messageBubbleColoursComeFromTheme 因 kBubbleOwn 缺失失败; core/message_renderer.cpp 8 处颜色改为 theme:: 常量)
Task 3: complete (tests: 34/34 pass; chatViewStylesRetryLink 先改断言 RED → 改 chat_view.cpp GREEN; 10 个文件迁移到 theme::; build_client.ps1 成功; 残留色扫描干净; 截图 8 张)
Task 4: complete (tests: 35/35 pass; RED 先行 messageActionBarHasSingleSoftShadow; message_action_bar.cpp 加 QGraphicsDropShadowEffect(blur 12, offset 0/4, rgba(0,0,0,10)); 截图确认单层投影)
Task 5: complete (README 更新; 9 张 screenshot-coinbase-*.png; 登录页用"备份/恢复 session 注册表、无 token 启动"抓取，未用密码、未触发服务端登出，会话恢复验证成功; 终验 35/35 + 构建成功 + 旧色扫描仅剩守卫测试)

Ruling: 计划原定最终 34 个测试，实际操作中为"操作条单层阴影"补了 TDD 守卫（messageActionBarHasSingleSoftShadow），最终 35 个 — 原因：阴影是规格中的验收项，配置值也需要可回归 — 代价：README 测试数按实际 35 更新。
Ruling: UI 层（全局命名空间）用文件内 `namespace theme = chatroom::client::theme;` 别名引用色板 — 原因：ui/*.cpp 不在 chatroom::client 命名空间、全限定名会让样式串过长 — 代价：引入了一个局部别名模式，如不合风格可全局替换。
Note: 断线横幅（红底）、发送/撤回新消息、@提及的视觉状态未在本次截图中触发（逻辑未改动）；如需可后续补测。

Review (requesting-code-review, fresh-context 子代理): Ready to merge = Yes；0 Critical、0 Important、6 Minor。跟进结果：
  - Minor#1 守卫测试路径改为相对可执行文件解析（保留旧路径回退）→ 已修
  - Minor#2 守卫扩展：QSS 中每个 `#rrggbb` 都必须在色板白名单内（正则扫描）→ 已修
  - Minor#3 friend_item_delegate 行圆角 10→8 对齐 4/8/12/16 刻度 → 已修
  - Minor#4 "发送失败" 文字改用 semantic-down（先加测试断言 RED → 渲染器加 span GREEN；重发链接保持蓝色）→ 已修
  - Minor#5 README 圆角描述补 18px 胶囊例外 → 已修
  - Minor#6 main_window.cpp 注释 "Telegram style" → "Coinbase-style" → 已修
  - 复查的 9 条 "declined to judge"（发行包/packaging 暂停、旧构建产物、11px muted-soft 对比度、QMenu 圆角渲染、英文 Save/Cancel 本地化、深色模式/字体/logo 非目标等）→ 留档，均超出本次范围
复核后终态：tests 35/35 绿；build_client.ps1 成功；最终构建运行正常（截图核对通过）
