# Coinbase 设计语言换肤实施计划

**Spec:** `docs/superpowers/specs/2026-10-19-coinbase-style-ui.md`
**设计来源:** `.opencode/skills/design-md/designs/coinbase.md`
**日期:** 2026-10-19

> **执行环境说明**：本工作区不是 git 仓库，因此计划中没有提交步骤；每个任务的"验证"= 运行测试 + 构建 + 截图核对。

## 目标

把 Qt 客户端（全界面）从 Telegram 浅色风格换成 Coinbase 设计语言：白底、冷灰表面、单一蓝色强调、hairline 分隔、Coinbase 圆角刻度、单层阴影。**零行为改动**。

## 架构

- **颜色单一来源（C++ 侧）**：新文件 `chatroom/client/core/ui_theme.h`，`chatroom::client::theme` 命名空间下的 `constexpr const char*` 色值。
- **颜色单一来源（QSS 侧）**：`chatroom/client/resources/style.qss`。QSS 无法 include C++ 常量，两处由单元测试 `themeTokensMatchStyleSheet` 守卫一致（新色必须都在 QSS 里、旧色必须都不在）。
- 气泡/元信息是 `QTextBrowser` 富文本 HTML，颜色在 `core/message_renderer.cpp` 里内联，同样引用 `theme::` 常量。
- 行为代码（`messageActions`、协议、存储）不碰。

## 全局约束

- 只改视觉与颜色常量；不改任何接口、协议、数据库、逻辑。
- 除身份色板（头像多色）外，C++ 源文件里不再出现字面 hex——一律用 `theme::` 常量；QSS 颜色只出现在 `style.qss`。
- 现有 32 个测试必须保持全绿；本计划新增 2 个测试、更新 1 个断言（最终 34 个）。
- 构建：`powershell chatroom/client/build_client.ps1`；测试：`powershell chatroom/client/tests/run_client_tests.ps1`。
- 不引入新依赖、字体文件、图片资源。
- 中文源文件必须用编辑工具修改（不用 PowerShell 文本重写）。
- 客户端当前运行实例：`chatroom/client/build/release/chatroom_client.exe`（验收时重启）。

## 色值总表（新旧对照）

**新色板（写入 `ui_theme.h` 与 `style.qss`）：**

| 常量 | hex | 用途 |
|---|---|---|
| `kAccent` | `#0052FF` | 主按钮、发送键、链接、未读徽标、选区、焦点边框 |
| `kAccentHover` | `#0046DD` | 主按钮 hover（派生） |
| `kAccentActive` | `#003ECC` | 主按钮/发送键 pressed |
| `kAccentDisabled` | `#A8B8CC` | 主按钮 disabled |
| `kCanvas` | `#FFFFFF` | 面板、卡片、对方气泡 |
| `kSurfaceSoft` | `#F7F7F7` | 窗口/聊天区背景、列表 hover |
| `kSurfaceStrong` | `#EEF0F3` | 输入框底、列表选中、菜单高亮、次级按钮底 |
| `kSurfaceStrongHover` | `#E6E8EC` | 次级按钮 hover（派生） |
| `kHairline` | `#DEE1E6` | 分隔线/边框 |
| `kInk` | `#0A0B0D` | 主文字 |
| `kBody` | `#5B616E` | 正文/次要按钮文字 |
| `kMuted` | `#7C828A` | 次要文字 |
| `kMutedSoft` | `#A8ACB3` | 更弱文字/禁用文字 |
| `kOnPrimary` | `#FFFFFF` | 主按钮文字 |
| `kSemanticUp` | `#05B169` | 在线/成功 |
| `kSemanticDown` | `#CF202F` | 错误 |
| `kBubbleOwn` | `#E6F7F0` | 己方气泡（`kSemanticUp` 10% tint，有意偏离） |
| `kBubbleOther` | `#FFFFFF` | 对方气泡 |
| `kBannerErrorBg` | `#FAE9EA` | 断线横幅底（派生 tint） |
| `kBannerErrorLine` | `#F2BCC0` | 断线横幅边（派生 tint） |

**旧色一律消失**（grep 验收对象）：`#3390EC #E7EBF0 #DCF8C6 #2B82D9 #2474C4 #E9EBEE #DDE1E6 #F4F4F5 #A5A9AE #C4C9CF #A9AFB6 #1A1A1A #707579 #E6E8EB #B00020 #444444 #666666 #888888 #555555 #666666 #BBBBBB #999999 #FDE7E9 #A4262C #F1BBBC #2E9E5B #EAF3FF`。例外（保留）：两个身份色板中的非蓝颜色（`#4CAF50 #F5A623 #9B59B6 #E74C3C #16A085`、`#5AD8A6 #F6BD16 #E8684A #6DC8EC #9270CA #FF9D4D #269A99`）——头像身份色，有意保留。

---

## 任务 1：`ui_theme.h` + 守卫测试 + `style.qss` 全量换色

**交付物：** 新主题常量头；QSS 新色板；测试 33 绿；主界面截图。

### 步骤

1. 新建 `chatroom/client/core/ui_theme.h`：

```cpp
#ifndef CHATROOM_CLIENT_CORE_UI_THEME_H
#define CHATROOM_CLIENT_CORE_UI_THEME_H

namespace chatroom {
namespace client {
namespace theme {

// Coinbase design language tokens (see docs/superpowers/specs/2026-10-19-coinbase-style-ui.md).
// Colours are uppercase here; resources/style.qss carries the same palette in
// lowercase and the themeTokensMatchStyleSheet test guards that both stay in sync.
constexpr const char* kAccent = "#0052FF";
constexpr const char* kAccentHover = "#0046DD";             // derived
constexpr const char* kAccentActive = "#003ECC";            // primary-active
constexpr const char* kAccentDisabled = "#A8B8CC";          // primary-disabled
constexpr const char* kCanvas = "#FFFFFF";
constexpr const char* kSurfaceSoft = "#F7F7F7";
constexpr const char* kSurfaceStrong = "#EEF0F3";
constexpr const char* kSurfaceStrongHover = "#E6E8EC";      // derived
constexpr const char* kHairline = "#DEE1E6";
constexpr const char* kInk = "#0A0B0D";
constexpr const char* kBody = "#5B616E";
constexpr const char* kMuted = "#7C828A";
constexpr const char* kMutedSoft = "#A8ACB3";
constexpr const char* kOnPrimary = "#FFFFFF";
constexpr const char* kSemanticUp = "#05B169";
constexpr const char* kSemanticDown = "#CF202F";
constexpr const char* kBubbleOwn = "#E6F7F0";               // 10% tint of kSemanticUp
constexpr const char* kBubbleOther = "#FFFFFF";
constexpr const char* kBannerErrorBg = "#FAE9EA";           // derived
constexpr const char* kBannerErrorLine = "#F2BCC0";         // derived

}  // namespace theme
}  // namespace client
}  // namespace chatroom

#endif  // CHATROOM_CLIENT_CORE_UI_THEME_H
```

2. 注册进工程：
   - `chatroom_client.pro`：`HEADERS += \` 列表加 `core/ui_theme.h \`
   - `tests/tests.pro`：`HEADERS += \` 列表加 `../core/ui_theme.h \`

3. 测试（**RED**）：`tests/test_client_core.cpp`
   - `#include <QFile>`、`#include "core/ui_theme.h"`；类声明加 `void themeTokensMatchStyleSheet();`
   - 实现（测试的工作目录是 `client/tests`，`run_client_tests.ps1` 已用 `Push-Location` 保证）：

```cpp
void TestClientCore::themeTokensMatchStyleSheet() {
    QFile style_file(QStringLiteral("../resources/style.qss"));
    QVERIFY2(style_file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(style_file.errorString()));
    const QString css = QString::fromUtf8(style_file.readAll()).toLower();

    // QSS 负责的颜色（App 从 :/style.qss 加载同一文件，构建时嵌入）。
    // 注意：kBody/kBubbleOwn/kBubbleOther/kSemanticUp/kSemanticDown/kBanner*
    // 只出现在 C++/HTML 里，不在 QSS，故不在此列表。
    const QStringList required = {
        theme::kAccent, theme::kAccentHover, theme::kAccentActive,
        theme::kAccentDisabled, theme::kCanvas, theme::kSurfaceSoft,
        theme::kSurfaceStrong, theme::kSurfaceStrongHover, theme::kHairline,
        theme::kInk, theme::kMuted, theme::kMutedSoft,
    };
    for (const QString& token : required) {
        QVERIFY2(css.contains(token.toLower()), qPrintable(token));
    }

    const QStringList retired = {
        "#3390ec", "#e7ebf0", "#2b82d9", "#2474c4", "#e9ebee",
        "#dde1e6", "#f4f4f5", "#a5a9ae", "#c4c9cf", "#a9afb6",
        "#707579", "#1a1a1a", "#e6e8eb",
    };
    for (const QString& token : retired) {
        QVERIFY2(!css.contains(token), qPrintable(token));
    }
}
```

   - 运行 `run_client_tests.ps1` → `themeTokensMatchStyleSheet` 失败（RED）。

4. 重写 `resources/style.qss` 颜色（逐条映射）：

| 行/选择器 | 旧 | 新 |
|---|---|---|
| `*` color | `#1A1A1A` | `#0A0B0D` |
| `QMainWindow, QDialog` bg | `#E7EBF0` | `#F7F7F7` |
| `QWidget#LoginPage` bg | `#E7EBF0` | `#F7F7F7` |
| `QFrame#LoginCard` border | `#DDE1E6` | `#DEE1E6` |
| `QLabel#AppTitle` color | `#1A1A1A` | `#0A0B0D` |
| `QLabel#AppSubtitle / #SectionLabel / #MutedLabel` | `#707579` | `#7C828A` |
| `QLineEdit, QSpinBox` bg/border | `#F4F4F5` / `#E6E8EB` | `#EEF0F3` / `#DEE1E6`（radius 9→8） |
| 同上 `selection-background-color` | `#3390EC` | `#0052FF`；新增 `selection-color: #FFFFFF;` |
| `QLineEdit:focus, QSpinBox:focus` border | `#3390EC` | `#0052FF`（bg `#FFFFFF` 不变） |
| `QLineEdit#MessageInput` bg/border | `#F4F4F5` / `#E6E8EB` | `#EEF0F3` / `#DEE1E6`（radius 18 不变） |
| `QLineEdit#MessageInput:focus` border | `#3390EC` | `#0052FF` |
| `QPushButton` bg | `#F4F4F5` | `#EEF0F3` |
| `QPushButton:hover` | `#E9EBEE` | `#E6E8EC` |
| `QPushButton:pressed` | `#DDE1E6` | `#DEE1E6` |
| `QPushButton:disabled` | `#A5A9AE` / `#F4F4F5` | `#A8ACB3` / `#F7F7F7` |
| `QPushButton#PrimaryButton` | `#3390EC` | `#0052FF` |
| `QPushButton#PrimaryButton:hover` | `#2B82D9` | `#0046DD` |
| `QPushButton#PrimaryButton:pressed` | `#2474C4` | `#003ECC` |
| 新增 `QPushButton#PrimaryButton:disabled` | — | `background: #A8B8CC; color: #FFFFFF;` |
| `QPushButton#SendButton` | `#3390EC` | `#0052FF` |
| `QPushButton#SendButton:hover` | `#2B82D9` | `#0046DD` |
| 新增 `QPushButton#SendButton:pressed` | — | `background: #003ECC;` |
| `QWidget#Sidebar` `#RightSidebar` `#ChatHeader` `#Composer` border | `#E6E8EB` | `#DEE1E6` |
| `QWidget#ChatPanel` bg | `#E7EBF0` | `#F7F7F7` |
| `QTextBrowser` bg | `#E7EBF0` | `#F7F7F7` |
| `QFrame#MessageActionBar` border / radius | `#DDE1E6` / 13px | `#DEE1E6` / 12px |
| `QFrame#MessageActionBar QPushButton` color / radius | `#3390EC` / 9px | `#0052FF` / 8px |
| ⋯ hover / pressed | `#F4F4F5` / `#E9EBEE` | `#F7F7F7` / `#EEF0F3` |
| `QStackedWidget` bg | `#E7EBF0` | `#F7F7F7` |
| `QStatusBar` border / color | `#E6E8EB` / `#707579` | `#DEE1E6` / `#7C828A` |
| `QStatusBar QLabel` color | `#707579` | `#7C828A` |
| `QListWidget::item` radius | 9px | 8px |
| `QListWidget::item:hover` | `#F4F4F5` | `#F7F7F7` |
| `QListWidget::item:selected` | `#3390EC` / `#FFFFFF` | `#EEF0F3` / `#0A0B0D` |
| `QScrollBar::handle:vertical` / `:hover` | `#C4C9CF` / `#A9AFB6` | `#A8ACB3` / `#7C828A` |
| `QCheckBox::indicator` border / radius | `#C4C9CF` / 5px | `#A8ACB3` / 4px |
| `QCheckBox::indicator:checked` bg / border | `#3390EC` | `#0052FF` |
| `QMenu` border | `#DDE1E6` | `#DEE1E6` |
| `QMenu::item` radius | 6px | 4px |
| `QMenu::item:selected` | `#3390EC` / `#FFFFFF` | `#EEF0F3` / `#0A0B0D` |
| `QToolTip` bg | `#1A1A1A` | `#0A0B0D` |

5. 运行 `run_client_tests.ps1` → 33 passed（**GREEN**）。

6. 构建（`build_client.ps1`），重启客户端，截主界面：浅灰底 + 白面板 + 蓝色发送键 + 浅灰选中行。

---

## 任务 2：消息渲染器换色（气泡与元信息）

**交付物：** `messageBubbleColoursComeFromTheme` 测试；渲染器 8 处颜色走 `theme::`；34 测试绿。

### 步骤

1. 测试（**RED**）：`tests/test_client_core.cpp` 新增声明与实现：

```cpp
void TestClientCore::messageBubbleColoursComeFromTheme() {
    const QDateTime created = chatroom::client::parseServerUtc("2026-10-19 12:00:00");
    chatroom::client::ChatMessage own;
    own.message_id = 1; own.client_msg_id = "c1"; own.content = "hi";
    own.created_at_utc = created; own.outgoing = true;
    own.status = chatroom::client::MessageStatus::kSent;
    chatroom::client::ChatMessage other;
    other.message_id = 2; other.sender_id = 9; other.sender_name = "peer";
    other.content = "yo"; other.created_at_utc = created;

    const QString html = chatroom::client::renderConversationHtml({own, other}, 1);
    QVERIFY(html.contains(QString::fromLatin1(theme::kBubbleOwn)));
    QVERIFY(html.contains(QString::fromLatin1(theme::kBubbleOther)));
    QVERIFY(!html.contains(QStringLiteral("#DCF8C6")));
    QVERIFY(!html.contains(QStringLiteral("#F2F2F2")));
}
```

   运行 → 失败（RED）。

2. 修改 `core/message_renderer.cpp`（`#include "core/ui_theme.h"`），8 处字面量全部换成 `theme::` 常量（用 `QString::fromLatin1(theme::kX)` 或字符串拼接，保持现有构造方式）：

| 行 | 旧 | 新 |
|---|---|---|
| 28 重试链接 | `#3390EC` | `theme::kAccent` |
| 76 日期分隔 | `#888888` | `theme::kMuted` |
| 84 己方气泡 | `#DCF8C6` | `theme::kBubbleOwn` |
| 84 对方气泡 | `#F2F2F2` | `theme::kBubbleOther` |
| 95 发送者名 | `#555555` | `theme::kBody` |
| 104 引用左边框 | `#BBBBBB` | `theme::kHairline` |
| 105 引用文字 | `#666666` | `theme::kMuted` |
| 112 已撤回 | `#999999` | `theme::kMutedSoft` |
| 117 时间元信息 | `#888888` | `theme::kMutedSoft` |

3. 运行 → 34 passed（**GREEN**）。

---

## 任务 3：C++ 界面颜色迁移（10 个文件）

**交付物：** 全部 C++/delegate 颜色走 `theme::`；链接色测试更新；34 绿；构建；`grep` 扫描干净。

### 步骤

1. `ui/chat_view.cpp`：删除 `const char kLinkColor[]`，`link_format.setForeground(QColor(theme::kAccent))`（`#include "core/ui_theme.h"`）。同时更新测试断言 `chatViewStylesRetryLink`（约 596 行）：`QCOMPARE(format.foreground().color(), QColor(theme::kAccent));` — 先改测试（RED），再改 `chat_view.cpp`（GREEN）。

2. `ui/friend_item_delegate.cpp`（`#include "core/ui_theme.h"`）：
   - 头像色板 `QColor("#3390EC")` → `QColor(theme::kAccent)`（其余 5 色保留）。
   - id==0 兜底文字 `#707579` → `theme::kMuted`。
   - 选中行底 `#3390EC` → `theme::kSurfaceStrong`；hover `#F4F4F5` → `theme::kSurfaceSoft`。
   - 在线点 `#4CAF50` → `theme::kSemanticUp`，描边 `Qt::white` → `QColor(theme::kCanvas)`。
   - 主文字：恒为 `theme::kInk`；次文字：恒为 `theme::kMuted`（不再有 selected 分支的特殊色）。
   - 未读徽标：底恒为 `theme::kAccent`、字恒为 `theme::kOnPrimary`（删除 selected 反色分支）。

3. `ui/main_window.cpp`：
   - 头像色板（6 色副本）`#3390EC` → `theme::kAccent`。
   - `MyProfileButton` 内联样式：border-top `#E6E8EB`→`theme::kHairline`；hover `#F4F4F5`→`theme::kSurfaceSoft`；pressed `#E9EBEE`→`theme::kSurfaceStrong`；背景 `#FFFFFF`→`theme::kCanvas`。
   - `my_profile_name_` `#1A1A1A`→`theme::kInk`；`my_profile_uid_` `#707579`→`theme::kMuted`。
   - 断线横幅：`#FDE7E9/#A4262C/#F1BBBC` → `theme::kBannerErrorBg / theme::kSemanticDown / theme::kBannerErrorLine`。
   - `setStatus`：`#b00020`→`theme::kSemanticDown`，`#444444`→`theme::kBody`。

4. `ui/login_page.cpp`：Logo 圆底 `#3390EC`→`theme::kAccent`，字色 `#FFFFFF`→`theme::kOnPrimary`；`setStatus` 同 3 的状态色。

5. `ui/profile_dialog.cpp`：
   - 计数标签：`#b00020`→`theme::kSemanticDown`，`#707579`→`theme::kMuted`。
   - 名字 `#1A1A1A`→`theme::kInk`；昵称/UID `#707579`→`theme::kMuted`；用户名/签名/状态 `#444444`→`theme::kBody`。
   - 签名 `QPlainTextEdit`：bg `#F4F4F5`→`theme::kSurfaceStrong`，border `#E6E8EB`→`theme::kHairline`（radius 9→8），focus border `#3390EC`→`theme::kAccent`。
   - 主按钮（发消息/加好友/保存）：`#3390EC/#2B82D9/#2474C4/#A5A9AE/#F4F4F5` → `kAccent/kAccentHover/kAccentActive/kMutedSoft/kSurfaceStrong`（disabled 背景 `theme::kSurfaceSoft`）。
   - 在线 `#2E9E5B`→`theme::kSemanticUp`，离线 `#A5A9AE`→`theme::kMutedSoft`。
   - `setStatus` 同 3。

6. `ui/settings_dialog.cpp`：`#1a1a1a`→`theme::kInk`；`#666666`→`theme::kMuted`。

7. `ui/user_search_dialog.cpp`、`ui/message_search_dialog.cpp`：状态色 `#b00020`→`theme::kSemanticDown`，`#444444`→`theme::kBody`。

8. `core/avatar_utils.cpp`：身份色板第一项 `#5B8FF9` → `QColor(theme::kAccent)`（其余 7 色保留）。

9. 验证：
   - 运行测试 → 34 passed。
   - `build_client.ps1` → 成功。
   - 扫描残留色（PowerShell，排除 build 目录，人工核对结果）：
     `Get-ChildItem -Recurse -Include *.cpp,*.h,*.qss | Where-Object { $_.FullName -notmatch '\\build\\' } | Select-String -Pattern '#[0-9A-Fa-f]{6}'`
     → 预期只剩：`core/ui_theme.h`（全部新色）、身份色板保留色（10 个）、`resources/style.qss`（新色板）、测试文件里的 `theme::` 引用（无字面 hex）。
   - 重启客户端，截图：登录页（退出登录→截图→重新登录）、主界面、资料页、设置、消息搜索、用户搜索。

---

## 任务 4：操作条单层阴影 + 细节

**交付物：** 操作条一层柔和阴影（Coinbase 单层 elevation）；截图。

### 步骤

1. `ui/message_action_bar.cpp`：构造时加

```cpp
auto* shadow = new QGraphicsDropShadowEffect(this);
shadow->setBlurRadius(12);
shadow->setOffset(0, 4);
shadow->setColor(QColor(0, 0, 0, 10));   // rgba(0,0,0,0.04)
setGraphicsEffect(shadow);
```

（`#include <QGraphicsDropShadowEffect>`；这是 QSS 无法表达的 `0 4px 12px rgba(0,0,0,0.04)` 的单层阴影。）

2. 构建 + 重启，截图：悬停操作条（回复/撤回）、右键菜单。

---

## 任务 5：文档与最终验收

**交付物：** README 更新；全套截图；最终验证报告。

### 步骤

1. `chatroom/client/README.md`：
   - 风格描述从 Telegram 改为 Coinbase 设计语言（引用 spec 路径 `../../docs/superpowers/specs/2026-10-19-coinbase-style-ui.md`）。
   - 测试数 32 → 34（新增 `themeTokensMatchStyleSheet`、`messageBubbleColoursComeFromTheme`）。
   - 截图清单增加 `screenshot-coinbase-*.png`。

2. 最终验收（逐项打勾，附证据）：
   - `run_client_tests.ps1` → 34 passed。
   - `build_client.ps1` → 通过。
   - 重启客户端，自动恢复会话，截图 9 张：登录、主界面、悬停操作条、右键菜单、资料页、设置、消息搜索、用户搜索、好友申请。
   - 对照 spec 逐项核对色板（发送键/链接=蓝 `#0052FF`、己方气泡=薄荷绿、对方气泡=白、选中行=浅灰、hairline 分隔、无第二品牌色）。
   - 旧色 grep 扫描干净。
   - 功能回归抽查：发消息、引用、撤回窗口、消息搜索、@ 提及、未读、重连（重连横幅颜色）。

## Review Focus（最可能出问题的点）

1. **QSS/内联样式遗漏**：回复条、断线横幅、资料页按钮等内联样式容易漏改 → grep 扫描 + 截图逐一核对。
2. **选中行对比过弱**：`#EEF0F3` 底 vs `#F7F7F7` hover 差异小 → 截图核对，必要时选中行加 `1px solid #DEE1E6` 边框。
3. **白气泡在浅灰底上边界不清**：`#FFFFFF` 气泡 vs `#F7F7F7` 聊天底 → 截图核对；若不可辨则把聊天底加深一档（仍用 `surface-soft`，或给气泡加 `<table>` 边框——富文本不可靠，优先调底色）。
4. **小字号对比不足**：11px 的 `#A8ACB3`（时间/已撤回）在浅底上偏淡 → 截图核对，必要时降到 `kMuted`。
5. **蓝色滥用**：Coinbase 规则"蓝色只给 CTA/链接/徽标/焦点" → 检查没有新增蓝色大面积填充；列表选中已改浅灰，属有意设计。
6. **发送键 pressed 状态**：旧 QSS 没有 `SendButton:pressed`，会掉到通用 pressed 灰色 → 任务 1 已补 `#003ECC`。

## 任务依赖

- 任务 1（常量+QSS）→ 任务 2、3（C++ 引用常量）→ 任务 4（细节）→ 任务 5（验收）。任务 2 与 3 可并行，但都在任务 1 之后。
