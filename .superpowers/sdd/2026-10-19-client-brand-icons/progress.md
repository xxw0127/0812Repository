# SDD ledger — plan: docs/superpowers/plans/2026-10-19-client-brand-icons.md

Spec: docs/superpowers/specs/2026-10-19-client-brand-icons.md
Executor: inline (本会话). 工作区非 git 仓库 → 以本台账代替提交记录。
设计定稿来源：brainstorm 会话 `.superpowers/brainstorm/11124-1792406202/`（app-icon.html → 选 B；room-icon.html → 选 R2）。

## Pre-flight 扫描（任务间共享接口）

| 生产者 → 消费者 | 接口 | 结论 |
|---|---|---|
| Task 1 → Task 3 | `:/app.ico`（资源存在，含 16/24/32/48/64/128/256 帧） | 一致 |
| Task 2 → Task 4 | `QImage roomAvatar(int size)` | 一致 |
| Task 2 与 Task 3 同改 `ui/main_window.cpp`（2630/2985 vs 542） | 不同区域 | 无冲突 |

## 任务进度

- Task 1: 多帧 app.ico — ✅ complete
- Task 2: roomAvatar 与大厅徽标接线 — ✅ complete
- Task 3: 托盘图标 — ✅ complete
- Task 4: 视觉验收与证据固化 — ✅ complete

### Task 3 记录

- 新增守卫测试 `trayIconUsesBrandIcon`（源文件扫描：`main_window.cpp` 不得再出现 `SP_MessageBoxInformation`，且必须出现 `:/app.ico`）。RED 形态：`'!source.contains(...SP_MessageBoxInformation...)' returned FALSE` ✓ 符合计划预期。
- 替换 `ui/main_window.cpp:542` → `tray_->setIcon(QIcon(QStringLiteral(":/app.ico")));`。GREEN：**38 passed / 0 failed**。
- **Ruling**: `build_client.ps1` 增加 `imageformats\qico.dll` 复制（与既有 qjpeg 同款模式）。理由：Task 3 之后窗口/托盘图标在运行时经 Qt 读取 `:/app.ico`，而该项依赖 ICO 图像插件；计划未覆盖部署插件清单，只复制 qjpeg 时干净机器上托盘图标会变空（窗口图标还能回退 exe 资源）。代价（若判断错）：多复制一个官方插件，无副作用。
- Task 3: complete (tests: run_client_tests.ps1 → 38 passed / 0 failed)

### Task 4 记录

- **Ruling**: 16/24 简化帧几何修订。初版 (28,34,200,174,r64 / 尾巴描边 18) 在真实 16px 帧上糊成白块、尾巴不可辨（Task 4 验收发现，与原型矢量的观感不符）；按规格"小尺寸可辨性"条款收紧为 **(36,42,184,156,r56 / 尾巴 (88,182)-(74,226)-(130,190) 描边 20)**，4 候选对比后选定。32px 及以上完整版几何未动；规格 §3.2 已同步修订说明。代价（若判断错）：仅影响 16/24 两帧观感，改动集中在生成器一处。
- 证据（`screenshot-coinbase-icons.png`，4 面板）：
  1. app.ico 真实帧阶梯（16/24 简化、32–128 带 C、256 全幅）——直接用 System.Drawing 按尺寸取帧；
  2. 任务栏窗口按钮：蓝底白气泡 + C，激活高亮 ✓（对比：旧 debug 实例曾同时在列）；
  3. 托盘溢出区截图：品牌图标在列 ✓ + 16px 原始像素放大（气泡+尾巴可辨）；
  4. 应用内：房间列表「大厅 (1/1000)」与聊天头部徽标 = 蓝底白气泡 ✓（200% 裁切）。
- README 已更新：`已实现` 增加品牌图标条目、`说明` 增加 `qico.dll` 运行时依赖说明、回归数字 35 → 38、截图清单追加 `screenshot-coinbase-icons.png`。
- 复核清单：`SP_MessageBoxInformation` 与 `avatarIcon(QString::number(` 在 `ui/main_window.cpp` 均为 0 处；测试 38 passed / 0 failed；构建成功。
- 现场备注：Qt Creator debug 实例当时在跑旧代码（任务栏/托盘可见旧图标），未干预用户进程；若用户看到的仍是旧图标，属于该实例而非新构建。
- Task 4: complete (evidence: screenshot-coinbase-icons.png; README + spec + ledger updated)

## 结论

图标计划（Task 1–4）全部完成：多帧 app.ico（可重复生成的 `tools/make_app_icon.ps1`）、品牌托盘图标、R2 大厅徽标；测试 38 passed / 0 failed。下一步（本计划之外）：在线成员列表不显示已上传头像的 bug，走 systematic-debugging 根因分析。

### Task 2 记录

- `core/avatar_utils.h/.cpp` 新增 `QImage roomAvatar(int size)`（256 单位设计空间 → 目标像素，圆底 `theme::kAccent` + 白气泡 + 尾巴描边 14/圆角连接）。
- 测试 `roomAvatarIsBrandBadge`：先 RED（编译错误 `'roomAvatar' is not a member of 'chatroom::client'`，符合计划预期形态）→ 实现 → GREEN。
- **Ruling**: `ui/main_window.cpp` 无 `using namespace`，avatar_utils 调用一律全限定（既有 `chatroom::client::circularAvatar(...)` 惯例）；计划里的裸 `roomAvatar(...)` 写法按此修正为 `chatroom::client::roomAvatar(...)`。代价（若判断错）：编译立刻失败，成本为零。
- 接线：`selectRoom` 头部徽标（原 L2630-2633）→ `QPixmap::fromImage(chatroom::client::roomAvatar(32))`；房间列表 item（原 L2985）→ `QIcon(QPixmap::fromImage(chatroom::client::roomAvatar(36)))`。旧 `avatarIcon(QString::number(...))` 两处调用已消失。
- 证据：测试 **37 passed / 0 failed**；构建成功；截图核对（release 实例）：房间列表「大厅 (1/1000)」= 蓝底白气泡 ✓，聊天头部徽标一致 ✓。
- 现象记录：机器上另有一个 **Qt Creator debug 构建实例**在跑（`build\Qt_5_15_2_MinGW_64_bit_Debug\debug\chatroom_client.exe`，18:26 启动，旧代码），不影响 release 构建/链接；核对截图用新 release 实例。
- Task 2: complete (tests: run_client_tests.ps1 → 37 passed / 0 failed; visual: room list + header badge)

### Task 1 记录

- 新增 `tools/make_app_icon.ps1`（PowerShell + System.Drawing，4× 超采样、DIB 16/24/32/48/64/128 + PNG 256，标准 ICONDIR）。
- `tests/tests.pro` 增加 `RESOURCES += ../resources/resources.qrc`；新增测试 `brandAppIconExposesSizes`。
- **Ruling**: 旧 `app.ico` 文件头不合规（`planes=0, bitCount=0, bReserved=0xdf`，取证见 Qt 诊断输出），Qt 直接拒读（"Unsupported image format"，qico 插件本身已加载）——这是旧图标发虚的根因之一。生成器按标准 ICONDIR 写出即可，计划无需改。代价（若判断错）：测试与 Windows 侧都会立刻暴露。
- **Ruling**: 计划 Task 1 Step 5 的"独立帧解析"由生成器内置自校验完成（每次生成即重解析 ICONDIR：`frames=7 16x16(dib),24x24(dib),32x32(dib),48x48(dib),64x64(dib),128x128(dib),256x256(png)`），不再单跑一条冗余命令。代价：无（同一校验、更早触发）。
- 证据：`run_client_tests.ps1` → **36 passed, 0 failed**（含 `brandAppIconExposesSizes`）；帧实样抽取核对（System.Drawing 按尺寸取帧成功：16/24 为简化版、32+ 完整版）；生成物 112197 字节。
- Task 1: complete (tests: run_client_tests.ps1 → 36 passed / 0 failed)
