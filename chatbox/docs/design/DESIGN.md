# ChatBox 客户端设计系统（DESIGN.md）

> 从 `client/core/ui_theme.cpp`、`client/resources/style.qss` 和现有组件反推，作为后续 UI 改动的单一规范来源。未实现的内容标记为「待办」，不当作现有规范。

## 1. 色彩

颜色只允许来自 `client/core/ui_theme.h` 的 token，以及 `style.qss` / `style-dark.qss` 中登记过的同一组值。C++ 直接绘制和消息 HTML 也必须使用 `theme::k*`，禁止裸十六进制色值。

| Token | 浅色 | 深色 | 用途 |
|---|---|---|---|
| `kAccent` | `#0052FF` | `#4D8DFF` | 主操作、链接、焦点环 |
| `kAccentHover` | `#0046DD` | `#3B7BEB` | 主操作悬停 |
| `kAccentActive` | `#003ECC` | `#2F6FDC` | 主操作按下 |
| `kCanvas` | `#FFFFFF` | `#0F1419` | 卡片、侧栏、输入框焦点底 |
| `kSurfaceSoft` | `#F7F7F7` | `#1A2027` | 画布、次级表面、悬停 |
| `kSurfaceStrong` | `#EEF0F3` | `#232B33` | 输入框、按钮、选中行 |
| `kSurfaceStrongHover` | `#E6E8EC` | `#2B343D` | 表面悬停、禁用主按钮底 |
| `kHairline` | `#DEE1E6` | `#323B44` | 1px 分隔线、气泡描边 |
| `kInk` | `#0A0B0D` | `#F2F4F7` | 标题、主要正文 |
| `kBody` | `#5B616E` | `#AEB6C0` | 正文、发送者名 |
| `kMuted` | `#616877` | `#8A929C` | 元信息、次级文字 |
| `kMutedSoft` | `#676D76` | `#79818B` | 时间戳、弱提示 |
| `kOnPrimary` | `#FFFFFF` | `#FFFFFF` | 主按钮文字 |
| `kSemanticUp` | `#05B169` | `#34D399` | 成功/在线填充 |
| `kSemanticUpText` | `#027A48` | `#6EE7B7` | 在线文字 |
| `kOnlineDot` | `#038A52` | `#34D399` | 在线圆点 |
| `kSemanticDown` | `#CF202F` | `#FF7A7A` | 错误、超限 |
| `kDangerText` | `#B42318` | `#FF8A80` | 删除/撤回等危险文字 |
| `kDisabledText` | `#6B7280` | `#8A929C` | 禁用主按钮文字 |
| `kQuoteBg` | `#F4F6F8` | `#202830` | 引用块背景 |
| `kBubbleOwn` | `#E6F7F0` | `#14332A` | 己方气泡 |
| `kBubbleOther` | `#FFFFFF` | `#1A2027` | 对方气泡 |
| `kBannerErrorBg` | `#FAE9EA` | `#3A2024` | 断开横幅底 |
| `kBannerErrorLine` | `#F2BCC0` | `#6B3035` | 断开横幅描边 |

约束：

- 正文对比度 ≥ 4.5:1，图形标记 ≥ 3:1。
- 禁用控件不参与 WCAG AA 对比度要求。
- 暗色下不得残留纯白卡片或纯黑正文。
- 新增颜色必须先加入 `ui_theme.cpp` 的 `ThemePalette`，再同步 QSS 和颜色守卫测试。

## 2. 字体

| 层级 | 字号 | 字重 | 用途 |
|---|---:|---:|---|
| 品牌标题 | 26px | 600 | 登录页 `AppTitle` |
| 资料名 | 22px | 600 | 个人主页昵称 |
| 会话标题 | 16px | 600 | 聊天头部 `ContextTitle` |
| 空状态标题 | 15px | 600 | `EmptyStateTitle` |
| 消息正文 | 14px | 400 | 气泡正文，行高 150% |
| 基准 | 13px | 400 | 按钮、输入、列表 |
| 发送者/引用 | 12px | 400 | 发送者名、引用块 |
| 元信息 | 11px | 400/600 | 日期、时间、分组标题、角标 |

全局最小字号为 **11px**；不再使用 10px 文本。

## 3. 间距与圆角

- 间距刻度：4 / 6 / 8 / 10 / 12 / 16 / 24px。
- 圆角刻度：8 / 12 / 16 / 18 / 28px。
  - 按钮、输入框、列表行：8–12px。
  - 登录卡片、操作条：12–16px。
  - 发送按钮、消息输入框：12px；历史登录页曾用 18px 胶囊，现已收窄。
  - 品牌字母头像：28px（56px 直径的一半）。
- 空状态容器内边距 24px；描述最大宽度 320px。
- 聊天三栏：左 240–360px，中拉伸，右 180–280px；窗口最小 880×560。

## 4. 组件

### 按钮

- 主按钮：`kAccent` 底、`kOnPrimary` 字、8px 圆角、44px 高。
- 次按钮：`kSurfaceStrong` 底、`kInk` 字，悬停 `kSurfaceStrongHover`。
- 危险文字按钮：透明底，`kDangerText`。
- 图标按钮：至少 32×32px；`更多` 为 32×32、20px 图标，图标本身是三个 3px 圆角方点。
- 键盘焦点：仅键盘导航时显示 2px `kAccent` 环；鼠标点击清除。

### 输入

- 标准输入：`kSurfaceStrong` 底、1px `kHairline`、8px 圆角；聚焦 `kCanvas` 底 + `kAccent` 描边。
- 消息输入：多行 `QTextEdit`，40–120px 自适应，12px 圆角；Enter 发送，Shift+Enter 换行，Ctrl+Enter 发送。
- 粘贴消息只保留纯文本。
- 2000 字节上限；超过 1800 显示计数，超过 2000 禁用发送并变红。

### 消息流

- 气泡：外层 1px `kHairline` 模拟描边，内层 `cellpadding=10`；Qt 富文本不支持 `border-radius` / `box-shadow`，不得尝试。
- 时间戳在气泡外下方；己方右对齐，对方左对齐。
- 同一发送者 3 分钟内不重复昵称；发送者名 12px `kBody`，不加粗。
- 引用块：`kQuoteBg` 背景、6px 内边距、12px `kMuted`，禁止左侧竖线（side-stripe）。
- 撤回：`kSurfaceSoft` 底、14px 斜体 `kMutedSoft`、不显示发送者名。
- 日期分隔：11px `kMuted`，上下 16px。

### 列表与侧栏

- 大厅列表 56–160px，超过约 3 个房间内部滚动。
- 好友/会话列表行高 56px；空列表显示空状态，不显示空白区域。
- 右侧“在线成员 (N)”置顶，成员列表拉伸，设置/退出登录固定在底部 32px 次级按钮。
- 好友申请未读使用右侧 16px `kAccent` 圆角角标，按钮文字恒为“好友申请”。

### 空状态

- 56px 单色 `kMutedSoft` 图标、15px 标题、13px `kMuted` 描述，垂直居中。
- 未选会话、无消息、无好友、搜索无结果四类文案见 `EmptyState` 接入点。

## 5. 主题

- 设置页“外观”：跟随系统 / 浅色 / 深色。
- 浅色加载 `style.qss`，深色加载 `style-dark.qss`；C++ 颜色通过 `theme::k*` 运行时切换。
- 默认跟随系统，启动时读取；切换后立即刷新 QSS 和当前消息 HTML。

## 6. 禁止项

- 不使用未登记的裸 hex 颜色。
- 不在 Qt 富文本中使用 `border-radius`、`box-shadow` 或 `border-left` side-stripe。
- 不把禁用主按钮做成褪色饱和蓝 + 白字（会像加载中）。
- 不把最危险操作放在最显眼位置。
- 不为状态变化新增图片资源；空状态使用现有图标着色。
- 不在鼠标点击时保留键盘焦点环。

## 7. 待办

- 跟随系统的运行时监听尚未实现，目前只在启动时读取系统明暗设置。
- 窗口宽度小于 1000px 时自动收起右栏尚未实现（可选）。
- 消息搜索暂不支持双击定位到历史位置。
- 截图已归档到 `client/screenshots/`，部分为历史界面，UI 改动后需按当前代码重拍。
