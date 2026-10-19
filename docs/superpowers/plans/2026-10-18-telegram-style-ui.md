# Telegram 风格浅色 UI 实施记录

**Spec:** `docs/superpowers/specs/2026-10-18-telegram-style-ui.md`

## 完成

- 新增 `resources/style.qss` 全局浅色主题。
- 新增 `resources.qrc`，内嵌 QSS、应用图标和搜索/设置/发送/退出/消息图标。
- 登录页改为居中白色卡片、圆形 Logo、蓝色主按钮。
- 主窗口改为三栏：300px 会话栏、聊天区、220px 成员栏。
- 房间/私聊/联系人/成员增加圆形首字头像；房间使用编号头像避免重复“大”字。
- 列表行高统一 48px，避免头像与文字拥挤。
- 聊天头部增加上下文头像。
- 输入区改为圆角输入框，发送按钮改为纸飞机图标。
- 统一 8px 间距、浅灰背景、白色面板、蓝色选中态。
- 使用 Fusion style + 高 DPI 缩放。
- 保留引用、撤回、搜索、@、未读、重连等全部功能。

## 验证

- 客户端 QtTest：18 passed。
- 应用构建：通过。
- 登录页截图：`client/screenshot-telegram-login2.png`
- 主窗口截图：`client/screenshot-telegram-final3.png`
- 手工检查：登录、房间切换、私聊、联系人、引用、撤回、消息搜索、搜索用户、设置、退出登录。

## 限制

- 聊天气泡仍是 `QTextBrowser` 的富文本表格，无法可靠使用 CSS 圆角；后续若要求完全 Telegram 气泡，需要换成 `QListView + delegate`。
- 暂未做深色模式。
- 未使用 Telegram 商标、logo 或官方资源。
