# Chatroom 客户端品牌图标实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用已确认的 B 标记（蓝底白气泡 + 镂空 C）替换应用图标与托盘图标，并用 R2 徽标（品牌蓝圆 + 白气泡）替换大厅的「绿圆 + 房间号」。

**Architecture:** 图标全部由同一套 256 单位几何规格派生：`.ico` 由新增的 `tools/make_app_icon.ps1`（PowerShell + System.Drawing）离屏渲染多帧后写出；大厅徽标由 `core/avatar_utils.cpp` 新增的 `roomAvatar(int size)` 用 QPainter 运行时绘制（可测试、随主题 token 走）；两处调用点在 `ui/main_window.cpp` 换掉旧的 `avatarIcon(...)`；托盘图标复用 `:/app.ico` 资源。

**Tech Stack:** C++17 / Qt 5.15.2 (MinGW 8.1) / qmake + mingw32-make / QtTest / PowerShell + .NET System.Drawing（仅用于生成 .ico 资产）。

**Spec:** `docs/superpowers/specs/2026-10-19-client-brand-icons.md`

## Global Constraints

- 构建链固定：Qt `D:\Qt\5.15.2\mingw81_64`、MinGW `D:\Qt\Tools\mingw810_64`；测试与构建分别用 `tests\run_client_tests.ps1`、`build_client.ps1`，不要自造命令。
- 颜色单一来源 `core/ui_theme.h`：徽标蓝必须用 `theme::kAccent`（`#0052FF`），不得新写死颜色。
- 构建客户端前必须先 `Stop-Process chatroom_client`（运行中的 exe 会导致链接失败）。
- 本工作区**不是 git 仓库**：每个任务以"更新台账 `.superpowers/sdd/2026-10-19-client-brand-icons/progress.md`"代替提交步骤。
- 不改动协议、服务端、数据库；用户头像的字母体系保持不变。
- 几何数值一律采用规格 `§3`，实施时不得即兴调整。

## Review Focus

1. **非 Qt 消费者读取 `.ico`**（资源管理器、任务栏、`app.rc` 编译进 exe 的 `IDI_ICON1`）：帧必须是标准 DIB(32bpp+AND mask) 或 256px PNG —— Task 1 加独立帧解析校验。
2. **高 DPI 下缺帧**（125%/150% 缩放需要 20/24px 帧；缺失会由系统缩放发虚）—— Task 1 的 `brandAppIconExposesSizes` 钉死 24 与 16 帧存在。
3. **小尺寸失真**：大厅徽标列表里只有 30px，气泡尾巴可能糊掉或溢出圆底 —— Task 2 的像素断言只覆盖中心/边缘/四角，尾巴形状靠 Task 4 截图核对；`.ico` 16/24 用简化帧（无 C）避免糊。
4. **托盘图标验证盲点**：Windows 默认把新托盘图标收进溢出区，任务栏条上看不到 —— Task 4 用溢出区弹层截图（best-effort），不通过则说明原因。
5. **Windows shell 图标缓存**：换了资源却"看起来没变"会造成误判 —— Task 4 先重启进程再看；如仍缓存旧图，记录现象并说明需要 `explorer.exe` 重启才能刷新（不强制执行）。

---

### Task 1: 多帧 app.ico（TDD：先加测试与资源，再写生成器）

**Files:**
- Modify: `chatroom/client/tests/tests.pro`（新增 `RESOURCES += ../resources/resources.qrc`）
- Modify: `chatroom/client/tests/test_client_core.cpp`（新增 `brandAppIconExposesSizes`）
- Create: `chatroom/client/tools/make_app_icon.ps1`
- Modify: `chatroom/client/resources/app.ico`（由脚本生成）

**Interfaces:**
- Consumes: `resources/resources.qrc`（已注册 `:/app.ico`）；规格 §3.1/§3.2 几何。
- Produces: `:/app.ico` 多帧资源（尺寸集合 {16,24,32,48,64,128,256}）；`tools/make_app_icon.ps1` 可重复生成；Task 3 直接引用 `:/app.ico`。

- [ ] **Step 1: 写失败测试**

`tests/tests.pro` 追加：

```qmake
RESOURCES += ../resources/resources.qrc
```

`tests/test_client_core.cpp` 在 `private slots:` 声明 `void brandAppIconExposesSizes();`，并实现：

```cpp
void ClientCoreTest::brandAppIconExposesSizes() {
    QIcon icon(QStringLiteral(":/app.ico"));
    QList<QSize> sizes = icon.availableSizes();
    std::sort(sizes.begin(), sizes.end(),
              [](const QSize& a, const QSize& b) { return a.width() < b.width(); });
    QList<QSize> expected;
    const int wanted[] = {16, 24, 32, 48, 64, 128, 256};
    for (int side : wanted) {
        expected.append(QSize(side, side));
    }
    QCOMPARE(sizes, expected);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -File chatroom/client/tests/run_client_tests.ps1`
Expected: `brandAppIconExposesSizes` FAIL —— 当前 `app.ico` 只有 64×64 一帧，`availableSizes()` 为 `[QSize(64,64)]`（若 `:/app.ico` 尚未随 rcc 生效则为空列表，同样失败）。

- [ ] **Step 3: 写生成器 `tools/make_app_icon.ps1`**

要求（PowerShell + `System.Drawing`，无第三方依赖）：
1. 在 256 单位设计空间绘制规格 §3.1（完整版）与 §3.2（简化版）几何，`SmoothingMode=AntiAlias`，先按 4× 超采样绘制再高质量缩放到目标像素。
2. 帧清单：`16→简化, 24→简化, 32/48/64/128/256→完整`。
3. ICO 封装：≤128 帧用 DIB（BITMAPINFOHEADER，height=2h，32bpp BGRA 自底向上 + 全零 AND mask，行宽 4 字节对齐）；256 帧用 PNG 字节、ICONDIRENTRY 宽高写 0。
4. 输出到 `<脚本目录>\..\resources\app.ico`（覆盖）；结尾用独立解析（直接读 ICONDIR）打印帧数与各帧宽高/格式，作为自校验。
5. 脚本可重复执行、幂等。

运行：`powershell -File chatroom/client/tools/make_app_icon.ps1`
Expected: 打印 `frames=7` 与 `16x16(bmp),24x24(bmp),32x32(bmp),48x48(bmp),64x64(bmp),128x128(bmp),256x256(png)`。

- [ ] **Step 4: 运行测试确认通过**

Run: `powershell -File chatroom/client/tests/run_client_tests.ps1`
Expected: 全绿；`brandAppIconExposesSizes` PASS（rcc 会因 `app.ico` 变化重编译资源）。

- [ ] **Step 5: 独立校验帧结构（不依赖 Qt）**

Run: `powershell -Command "& { $b=[IO.File]::ReadAllBytes('chatroom/client/resources/app.ico'); $n=[BitConverter]::ToUInt16($b,4); \"count=$n\"; for($i=0;$i -lt $n;$i++){ $o=6+$i*16; $w=$b[$o]; if($w -eq 0){$w=256}; $h=$b[$o+1]; if($h -eq 0){$h=256}; $off=[BitConverter]::ToUInt32($b,$o+12); $fmt= if([BitConverter]::ToUInt32($b,$off) -eq 0x474E5089){'png'}else{'dib'}; \"$w x $h $fmt\" } }"`
Expected: `count=7` 且 7 行尺寸/格式与 Step 3 一致（证明非 Qt 工具也能读到全部帧）。

- [ ] **Step 6: 更新台账**

`.superpowers/sdd/2026-10-19-client-brand-icons/progress.md` 记录：生成器路径、帧清单、测试结果、独立校验输出。

---

### Task 2: roomAvatar() 与大厅徽标接线

**Files:**
- Modify: `chatroom/client/core/avatar_utils.h`（声明）、`chatroom/client/core/avatar_utils.cpp`（实现）
- Modify: `chatroom/client/ui/main_window.cpp:2630-2633`（聊天头部徽标）、`ui/main_window.cpp:2985`（房间列表 item 图标）
- Test: `chatroom/client/tests/test_client_core.cpp`（新增 `roomAvatarIsBrandBadge`）

**Interfaces:**
- Consumes: `theme::kAccent`（`core/ui_theme.h`，`avatar_utils.cpp` 已包含）；规格 §3.3 几何。
- Produces: `QImage chatroom::client::roomAvatar(int size)` —— 任意 size 下的品牌房间徽标（圆形 + 白气泡，四角透明）；Task 4 依据它做视觉核对。

- [ ] **Step 1: 写失败测试**

`tests/test_client_core.cpp` 声明并实现：

```cpp
void ClientCoreTest::roomAvatarIsBrandBadge() {
    const QImage img = chatroom::client::roomAvatar(36);
    QCOMPARE(img.size(), QSize(36, 36));
    QCOMPARE(img.pixelColor(18, 18), QColor(Qt::white));       // 气泡体中心
    QCOMPARE(img.pixelColor(4, 18), QColor(chatroom::client::theme::kAccent));  // 圆底
    QCOMPARE(img.pixelColor(0, 0).alpha(), 0);                 // 圆形徽标，四角透明
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -File chatroom/client/tests/run_client_tests.ps1`
Expected: 编译失败（`roomAvatar` 未声明）——这是预期的失败形态。

- [ ] **Step 3: 实现 `roomAvatar`**

`core/avatar_utils.h` 追加声明与注释（风格对齐现有函数）：

```cpp
// Draws the brand room badge (accent disc + white speech bubble) used for
// rooms in the sidebar and chat header. Geometry: see
// docs/superpowers/specs/2026-10-19-client-brand-icons.md §3.3.
QImage roomAvatar(int size);
```

`core/avatar_utils.cpp` 实现：直接按目标 `size` 绘制（`Format_ARGB32_Premultiplied`、`Antialiasing`），用 `p.scale(size / 256.0, size / 256.0)` 进入 256 单位空间，依次画圆底（`theme::kAccent`）、气泡体圆角矩形、尾巴路径（`QPen(Qt::white, 14, ..., Qt::RoundCap, Qt::RoundJoin)` 同时描边+填充）；`size <= 0` 返回空 `QImage`。

- [ ] **Step 4: 运行测试确认通过**

Run: `powershell -File chatroom/client/tests/run_client_tests.ps1`
Expected: `roomAvatarIsBrandBadge` PASS，其余测试不回归。

- [ ] **Step 5: 接线两处调用点**

- `ui/main_window.cpp:2630-2633`（`selectRoom`）：`context_avatar_->setPixmap(QPixmap::fromImage(roomAvatar(32)));`
- `ui/main_window.cpp:2985`（房间列表）：`item->setIcon(QIcon(QPixmap::fromImage(roomAvatar(36))));`（列表 iconSize 已是 30×30）
- 检查 `main_window.cpp` 是否仍需要 `<QPixmap>`/`QIcon` 既有包含（两者本就在用，无需新增）；`avatarIcon(QString::number(...))` 的两处调用彻底移除。

- [ ] **Step 6: 构建并粗验**

Run: `powershell -Command "Stop-Process chatroom_client -ErrorAction SilentlyContinue; powershell -File chatroom/client/build_client.ps1"`
Expected: `Build complete: …\build\release\chatroom_client.exe`；启动后房间列表与聊天头部显示蓝底白气泡徽标（Task 4 做正式截图）。

- [ ] **Step 7: 更新台账**

记录：`roomAvatar` 签名、两处调用点行号、测试结果、粗验观察。

---

### Task 3: 托盘图标改用品牌资源

**Files:**
- Modify: `chatroom/client/ui/main_window.cpp:542`
- Test: `chatroom/client/tests/test_client_core.cpp`（新增 `trayIconUsesBrandIcon`，源文件守卫）

**Interfaces:**
- Consumes: Task 1 的 `:/app.ico`；`tray_`（`QSystemTrayIcon*`，在 `main_window.cpp:540-544` 注册）。
- Produces: 运行时托盘图标 = 品牌标记（16/24 简化帧）。

- [ ] **Step 1: 写失败测试（源文件守卫）**

```cpp
void ClientCoreTest::trayIconUsesBrandIcon() {
    QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/../../ui/main_window.cpp");
    if (!QFile::exists(path)) {
        path = QStringLiteral("../ui/main_window.cpp");
    }
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
    const QString source = QString::fromUtf8(file.readAll());
    QVERIFY2(!source.contains(QStringLiteral("SP_MessageBoxInformation")),
             "tray must not fall back to the stock Qt info icon");
    QVERIFY2(source.contains(QStringLiteral(":/app.ico")),
             "tray must use the brand icon resource");
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -File chatroom/client/tests/run_client_tests.ps1`
Expected: `trayIconUsesBrandIcon` FAIL —— `main_window.cpp` 仍含 `SP_MessageBoxInformation`。

- [ ] **Step 3: 替换托盘注册行**

`ui/main_window.cpp:542`：

```cpp
    tray_->setIcon(QIcon(":/app.ico"));
```

（该行替换后 `style()` 在本文件仍有其它调用，无需清理包含头。）

- [ ] **Step 4: 运行测试确认通过**

Run: `powershell -File chatroom/client/tests/run_client_tests.ps1`
Expected: 全绿（此时应有 36 个真实测试 + init/cleanup 2 条 = 38 条 PASS）。

- [ ] **Step 5: 更新台账**

记录：守卫测试意图、替换行、测试计数。

---

### Task 4: 视觉验收与证据固化

**Files:**
- Create: `chatroom/client/screenshot-coinbase-icons.png`（任务栏/托盘/徽标拼图）
- Modify: `chatroom/client/README.md`（截图清单追加一行，若清单结构允许）
- Modify: `.superpowers/sdd/2026-10-19-client-brand-icons/progress.md`

**Interfaces:**
- Consumes: Task 1–3 产物（多帧 ico、roomAvatar、托盘资源）。
- Produces: 验收证据（截图 + 台账结论），供后续复查/打包使用。

- [ ] **Step 1: 重建并启动客户端**

Run: `powershell -Command "Stop-Process chatroom_client -ErrorAction SilentlyContinue; powershell -File chatroom/client/build_client.ps1; Start-Process 'chatroom/client/build/release/chatroom_client.exe'"`
Expected: 进程启动、窗口出现（若需要登录，走既有会话恢复路径，不动注册表令牌）。

- [ ] **Step 2: 抓取三处证据**

1. 任务栏：截图主屏底部条带，确认窗口按钮图标为 B 标记（深色任务栏）。
2. 托盘：点开溢出区（`^`）弹层截图；若弹层无法可靠抓取，记录原因并把托盘验证降级为"资源尺寸测试 + 任务栏观感"。
3. 应用内：房间列表 + 聊天头部徽标截图（窗口相对坐标随 rect 动态取值，用既有截图脚本模式）。

- [ ] **Step 3: 与原型比对**

与 `.superpowers/brainstorm/11124-1792406202/content/room-icon.html` 的 R2 卡片、`app-icon.html` 的 B 卡片逐项比对（形状、配色、小尺寸可辨性）；有偏差先修几何再重抓。

- [ ] **Step 4: 固化证据**

把三张截图拼成 `chatroom/client/screenshot-coinbase-icons.png`；README 截图清单追加 `screenshot-coinbase-icons.png`（图标：应用/托盘/大厅徽标）；台账写验收结论（含高 DPI/缓存注意事项）。

- [ ] **Step 5: 收尾**

确认无旧色/旧图形残留（`SP_MessageBoxInformation`、`avatarIcon(QString::number(` 均不再出现）；汇总测试计数与构建结果；把"在线成员头像 bug"标记为下一步任务并交回主线。
