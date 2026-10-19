# 客户端 Coinbase 设计语言换肤

**日期：** 2026-10-19
**设计来源：** `.opencode/skills/design-md/designs/coinbase.md`（skill `design-md`）

## 目标

在不改变任何聊天行为、协议与数据库的前提下，把 Qt 客户端从 Telegram 浅色风格迁移到 Coinbase 设计语言：白底、冷灰表面、单一蓝色强调、hairline 分隔、pill/圆角刻度、单层阴影。

范围：全客户端（登录/注册、主界面、资料页、设置、消息搜索、用户搜索、好友申请等）。

## 色板（Coinbase token → 本项目用途）

| 用途 | Coinbase token | hex |
|---|---|---|
| 强调/主按钮/链接/未读徽标 | primary | `#0052FF` |
| 主按钮 hover（派生，Coinbase 无 hover token） | — | `#0046DD` |
| 主按钮 pressed | primary-active | `#003ECC` |
| 主按钮 disabled | primary-disabled | `#A8B8CC` |
| 面板/卡片/气泡（对方） | canvas | `#FFFFFF` |
| 窗口/聊天区背景、hover 表面 | surface-soft | `#F7F7F7` |
| 输入框底、选中行、菜单高亮 | surface-strong | `#EEF0F3` |
| 分隔线 | hairline | `#DEE1E6` |
| 次级分隔/浅底 | hairline-soft | `#EEF0F3` |
| 主文字/标题 | ink | `#0A0B0D` |
| 正文/次要按钮文字 | body | `#5B616E` |
| 次要文字（时间、提示） | muted | `#7C828A` |
| 更弱文字（已撤回、禁用） | muted-soft | `#A8ACB3` |
| 主按钮文字 | on-primary | `#FFFFFF` |
| 在线/成功/己方气泡语义 | semantic-up | `#05B169` |
| 错误/失败 | semantic-down | `#CF202F` |
| 错误横幅底/边（派生 tint） | — | `#FAE9EA` / `#F2BCC0` |
| 己方气泡（见偏离 1） | — | `#E6F7F0`（`#05B169` 10% tint） |

圆角只用 Coinbase 刻度：`4 / 8 / 12 / 16 / 24 / pill`；间距基准 4px；阴影只有一层：`0 4px 12px rgba(0,0,0,0.04)`。

## 旧 → 新映射

| 位置 | 旧值 | 新值 |
|---|---|---|
| 窗口/聊天区背景 | `#E7EBF0` | `#F7F7F7` |
| 主色（按钮/链接/选中） | `#3390EC` | `#0052FF` |
| 主按钮 hover / pressed | `#2B82D9` / `#2474C4` | `#0046DD` / `#003ECC` |
| 自己气泡 | `#DCF8C6`（渲染器实际值） | `#E6F7F0` |
| 对方气泡 | `#F2F2F2` | `#FFFFFF` |
| 主文字 | `#1A1A1A` | `#0A0B0D` |
| 次要文字 | `#707579` / `#444444` / `#666666` | `#7C828A`（更弱用 `#A8ACB3`） |
| 分隔线 | `#E6E8EB` / `#DDE1E6` | `#DEE1E6` |
| 输入框底 | `#F4F4F5` | `#EEF0F3` |
| 列表选中态 | `#3390EC` 底 + 白字 | `#EEF0F3` 底 + `#0A0B0D` 字 |
| 错误文字 | `#b00020` | `#CF202F` |
| 在线文字 | `#2E9E5B` | `#05B169` |
| 滚动条手柄 | `#C4C9CF` / `#A9AFB6` | `#A8ACB3` / `#7C828A`（派生） |

## 组件规则

- 蓝色只用于：主按钮、发送键、链接（含重试链接）、未读徽标、复选框选中、文本选区、聚焦边框。
- 列表行：hover = `surface-soft`，selected = `surface-strong` + `ink` 文字（不做蓝色底）。
- 菜单：白底 + hairline + 8px 圆角，选中项 = `surface-strong` + ink 文字。
- 输入框：`#EEF0F3` 填充 + 透明边；聚焦 = 白底 + `#0052FF` 1px 边。
- 工具提示：`#0A0B0D` 底 + 白字。
- 主按钮/接收按钮保持 600 字重；正文 13px（桌面密度，见偏离 5）。

## 有意偏离（记录在案）

1. **己方气泡保留绿色语义**：用 `semantic-up #05B169` 的 10% tint `#E6F7F0`（用户决定）——Coinbase 原文语义色仅用于文字，此处作为己方消息标识。
2. **头像身份色板保留多色**：头像是身份标识而非品牌色；两处色板中的蓝色条目对齐 `#0052FF`。
3. **对方气泡不加边框**：`QTextBrowser` 富文本表格不支持可靠的圆角/边框，维持现状（无边框白色气泡）。
4. **QSS 限制**：`box-shadow` 与渐变不存在——单层阴影用 `QGraphicsDropShadowEffect`；营销用的渐变/网格不实现。
5. **字号**：正文保持 13px（Coinbase 16px 为网页正文，桌面聊天窗口更紧凑；层级/字重按 Coinbase）。
6. **字体**：沿用 `Segoe UI / Microsoft YaHei UI`（Coinbase 专有字体不引入，Inter 仅记录为替代）。

## 非目标

- 不改行为、协议、数据库；不引入深色模式；不使用 Coinbase 商标、logo、字体文件。

## 验收

- 全部界面截图：登录、主界面、悬停操作条、右键菜单、资料页、设置、消息搜索、用户搜索/好友申请。
- 客户端测试全绿（新增 2 个：token/QSS 漂移守卫、气泡配色；更新 1 个：重试链接色）。
- `build_client.ps1` 构建成功，重启后会话自动恢复。
- 全客户端 grep 不再出现旧品牌色（`#3390EC`、`#E7EBF0`、`#DCF8C6`、`#2B82D9`、`#2474C4`、`#E6E8EB`、`#1A1A1A`、`#707579`），`ui_theme.h` 与身份色板除外。
