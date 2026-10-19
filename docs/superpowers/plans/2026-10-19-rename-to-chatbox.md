# ChatBox 全量统一（chatroom → chatbox）实施计划

> **For agentic workers:** 本计划由本会话以 executing-plans 方式内联执行。步骤用 checkbox 跟踪。

**Goal:** 把"产品显示名 ChatBox / 内部标识符 chatroom"的分裂状态彻底统一：仓库目录、可执行文件、C++ 命名空间与头文件宏、CMake 目标、脚本、文档，以及线上服务器（目录、systemd 服务名、证书目录与服务器证书 CN、日志、nginx、MySQL 库名与用户名）全部改为 chatbox/ChatBox。

**Architecture:** 改动分三层。①仓库/代码层：机械重命名（大小写敏感三档替换 `chatroom→chatbox`、`Chatroom→ChatBox`、`CHATROOM→CHATBOX`），编译器和 41 个既有测试兜底。②客户端兼容层：QSettings 存储标识符变化会导致丢会话，新增一次性迁移（旧键 `Chatroom/ChatroomClient` → 新键 `ChatBox/ChatBox`），带单元测试。③线上层：服务器目录/服务/证书/日志/nginx/数据库改名，CA 保持不动（现有客户端按 SAN 中的 IP 校验，无需换 ca.crt）。

**Tech Stack:** C++17 / Qt 5.15.2 (MinGW 8.1) / qmake + mingw32-make / QtTest；服务端 CMake + g++ (Ubuntu 26.04)；PowerShell 本机脚本；ssh + scp（唯一传输通道，AGENTS.md 强制）；MySQL 8.4（部署机）。

## Global Constraints

- 构建链固定：Qt `D:\Qt\5.15.2\mingw81_64`、MinGW `D:\Qt\Tools\mingw810_64`；客户端构建/测试分别用 `chatbox\client\build_client.ps1`、`chatbox\client\tests\run_client_tests.ps1`。
- 构建/改名客户端前必须先停掉 `chatroom_client.exe`（运行中的 exe 会锁住目录，Windows 下目录无法改名）。
- 替换规则（严格区分大小写，按此顺序执行以避免二次替换）：`CHATROOM`→`CHATBOX`、`Chatroom`→`ChatBox`、`chatroom`→`chatbox`。
- 不替换的文件/目录：`build/`、`dist/`、`logs/`、`__pycache__/`、`.qtcreator/`、`*.png`、`*.ico`、`*.exe`、`*.a`、`*.o`、`*.stash`、`Makefile*`、`results*.txt`。
- 历史记录不重写：`docs/superpowers/plans|specs/`（历史计划/规格）、`.superpowers/**`（台账）、旧截图内的旧品牌字样保留原样。
- 服务器操作：只走 `ssh chatroom-dev`（=xxw@192.168.2.113，ed25519 密钥）与 scp；远端脚本一律 ASCII-only，中文路径用 `~/*/0917/` 通配展开；密码只经 `MYSQL_PWD`/env 传递，不得出现在命令文本或日志中。
- 现有 CA（`CN=Chatroom Dev CA`）**不重新生成**：只重签服务器证书（新 CN=chatbox.local，SAN 保留 `IP:192.168.2.113`），客户端 ca.crt 不变、无需升级。
- 改名不改行为：协议字节、数据库表结构、消息数据不得变化。
- 服务切换期间服务端停机约几十秒；切换前先备份数据库与旧配置，保留回滚路径。

## Review Focus

1. **Windows 目录锁**：运行中的 `chatroom_client.exe`（release 实例 + Qt Creator debug 实例）会阻止 `chatroom → chatbox` 目录改名；必须先停进程，确认 `git mv` 成功。
2. **QSettings 迁移可靠性**：迁移必须在任何 `QSettings` 读取之前执行，且旧键保留（回滚安全）；验证方式 = 注册表键值对比 + 启动后自动登录免重登。
3. **替换残留**：仓库内除白名单外 `chatroom` 残留必须为 0；`build/` 里的旧绝对路径会失效，客户端构建采用干净重建。
4. **服务器证书链**：重签服务器证书时误用新 CA 会让所有现有客户端拒绝连接——必须先核对 `ca.crt` 指纹一致（`openssl x509 -fingerprint`）再 reload nginx。
5. **数据库改名后服务端默认值**：服务端二进制 `db_user/db_name` 默认值改为 chatbox 后必须重新编译部署；切换前用旧库/旧用户可回滚（旧库保留到验证通过）。
6. **systemd 切换窗口**：先装新 unit（`chatbox.service`）再 stop 旧 → disable 旧 → enable --now 新，保证任何时刻只有一个服务监听。

---

### Task 0: 准备（停进程、分支、服务器备份）

**Files:** 无代码改动。

- [ ] 停止 release 与 debug 两个 `chatroom_client.exe` 进程；确认目录无占用（`handle` 不可用时用改名试探）。
- [ ] 建分支：`git switch -c rename-to-chatbox`（从 `main`）。
- [ ] 服务器备份（远程，只读+备份）：`mysqldump chatroom` → `~/backup/chatroom-YYYYmmdd.sql`；`cp -r /etc/chatroom /home/xxw/backup/etc-chatroom-YYYYmmdd`；`cp /etc/systemd/system/chatroom.service ~/backup/`；`cp /etc/nginx/stream.d/chat.conf ~/backup/`。

### Task 1: 仓库结构改名

**Files:**
- Move: `chatroom/` → `chatbox/`（`git mv`，连同被忽略的 build/logs/dist 一起移动）
- Move: `chatbox/client/chatroom_client.pro` → `chatbox/client/chatbox.pro`
- Move: `chatbox/conf/chatroom.env.example` → `chatbox/conf/chatbox.env.example`
- Move: `chatbox/deploy/chatroom.service` → `chatbox/deploy/chatbox.service`
- Move: `chatbox/deploy/chatroom.logrotate` → `chatbox/deploy/chatbox.logrotate`
- Modify: `.gitignore`（`chatroom/...` 路径 → `chatbox/...`；明文口令条目 → `chatbox/conf/chatbox.env`）

- [ ] `git mv chatroom chatbox`；4 个文件 `git mv` 改名。
- [ ] 更新 `.gitignore`；`git status` 应显示改名而非删除+新增。

### Task 2: 代码/脚本/部署文件内容替换

**Files:** `chatbox/**` 下所有文本文件（≈123 文件 / ≈1017 处），含：
- 客户端：命名空间/宏、`main.cpp` 品牌标识（本轮只改标识符，迁移代码在 Task 3）、`resources/app.rc`（InternalName/OriginalFilename=chatbox.exe）、`build_client.ps1`（`..\chatbox.pro`、输出 `chatbox.exe`）、`package_client.ps1`（`chatbox-client-0.2.0` → 见下）、`installer/chatbox.iss`（MyAppExeName=chatbox.exe）
- 命名统一（决定）：安装包/ZIP 名前缀 = `chatbox-0.2.0`（`package_client.ps1` 的 `$name = "chatbox-$Version"`，输出 `dist\chatbox-0.2.0-win64.zip`）
- 服务端：`server/config.h`（`log_file="logs/chatbox.log"`、`db_user="chatbox"`、`db_name="chatbox"`）、`server/config.cpp`（帮助文本 + `CHATBOX_DB_PASSWORD`）
- `CMakeLists.txt`：目标 `chatbox_common`/`chatbox_metrics`/`chatbox_net`/`chatbox_server`、`project(chatbox_server)`
- `tools/run_server.sh`（`conf/chatbox.env`、`build/chatbox_server`）
- `conf/chatbox.env.example`（`CHATBOX_DB_PASSWORD`）
- `deploy/chatbox.service`（`Description=ChatBox TCP Framework`、`WorkingDirectory=/home/xxw/桌面/0917/chatbox`、`ExecStart=.../chatbox/tools/run_server.sh ...`）
- `deploy/chatbox.logrotate`（新日志路径 + `systemctl kill -s USR1 chatbox.service`）
- `deploy/schema.sql`（`CREATE DATABASE chatbox` / `USE chatbox`）、`deploy/migrate_*.sql`（`USE chatbox;`）
- `deploy/gen_certs.sh`（`CERT_DIR=/etc/chatbox/certs`、CN `ChatBox Dev CA` / `chatbox.local`、SAN `DNS:chatbox.local`；**注释注明 CA 重生成需重新分发 ca.crt**）
- `deploy/nginx-chat-stream.conf`（证书路径 `/etc/chatbox/certs/`）、`deploy/setup_nginx.sh`

- [ ] 写一次性替换脚本（PowerShell + .NET UTF-8 读写，避免 PS5.1 ANSI 破坏文件），对白名单外文本文件执行三档替换。
- [ ] 校验：`Select-String -Pattern 'chatroom|Chatroom|CHATROOM'`（排除白名单）结果为 0。

### Task 3: 客户端设置迁移（TDD）

**Files:**
- Create: `chatbox/client/core/app_settings.h` / `app_settings.cpp`
- Modify: `chatbox/client/main.cpp`（`setOrganizationName("ChatBox")`、`setApplicationName("ChatBox")`、启动时先迁移）
- Modify: `chatbox/client/chatbox.pro`（SOURCES/HEADERS 增加 app_settings）
- Modify: `chatbox/client/tests/tests.pro`、`chatbox/client/tests/test_client_core.cpp`（新增迁移测试 + 更新既有"internal identifiers"守卫测试）

**Interfaces:**
- `namespace chatbox { bool migrateLegacySettings(QSettings &current, QSettings &legacy); }`
  - 语义：`legacy.allKeys()` 非空且 `current.allKeys()` 为空时，拷贝全部键值 → `current.sync()` → 返回 true；否则 false。
- main.cpp 用法：`QSettings current;`（新键）→ `if (current.allKeys().isEmpty()) { QSettings legacy(QSettings::UserScope, "Chatroom", "ChatroomClient"); chatbox::migrateLegacySettings(current, legacy); }`

- [ ] Step 1: 先写测试 `appSettingsMigrationCopiesLegacyValues`（IniFormat + 临时目录，旧库写两键 → 迁移 → 新库读回；再验证第二次调用返回 false、不覆盖已有值）；运行确认失败。
- [ ] Step 2: 实现 `app_settings.{h,cpp}` + main.cpp 接线 + .pro 更新；测试转绿。
- [ ] Step 3: 更新守卫测试（品牌标识符断言：exe/目录/QSettings/服务名现在应当是 chatbox/ChatBox）。

### Task 4: 文档更新

**Files:**
- Create: `README.md`（根目录，当前是 `# 0812Repository / NULL` 占位）：项目简介、目录结构、构建与运行（客户端/服务端）、测试命令、部署概述、截图索引
- Modify: `chatbox/README.md`（服务端 README：DB 用户/授权命令、env 文件、服务名、日志路径、证书 SAN/CN、客户端路径、ZIP 名；验收段落中的 `chatbox/nginx/mysql active` 由替换覆盖）
- Modify: `chatbox/client/README.md`（品牌与内部标识符现状、路径、ZIP 名、日志目录 `%APPDATA%\ChatBox\ChatBox\logs`、迁移说明）
- Modify: `docs/2026-10-18-linux-chatroom-design.md`、`docs/ssh-key-only.md`（活文档，产品引用与路径更新；SSH 别名 `chatroom-dev` 保留并注明）

- [ ] 完成上述文档更新；一致性检查（README 中的命令逐条与实际文件对照）。

### Task 5: 本地验证

- [ ] 跑 `chatbox\client\tests\run_client_tests.ps1`：全部通过（原 41 + 新增迁移/守卫测试）。
- [ ] 干净重建 release：删 `chatbox\client\build\`（旧绝对路径失效）→ `build_client.ps1` → 产出 `chatbox.exe`。
- [ ] 启动 `chatbox\client\build\release\chatbox.exe`：窗口标题 `ChatBox`、**自动登录成功**（QSettings 迁移生效）、成员/头像正常。
- [ ] 注册表核对：`HKCU\Software\ChatBox\ChatBox` 有 session 键；`HKCU\Software\Chatroom\ChatroomClient` 仍存在（回滚安全）。
- [ ] 证据截图：`chatbox/client/screenshot-chatbox-rename.png`（标题+登录态）。

### Task 6: 提交与推送

- [ ] 分支上分两个提交：①机械改名（目录+内容）②设置迁移与文档；或单提交（视 diff 可读性）。
- [ ] `git switch main` → `git merge --no-ff rename-to-chatbox` → `git push`（远端旧名重定向不影响）。
- [ ] 推送后检查：`git ls-files | wc -l` 无新增构建产物。

### Task 7: 线上数据库改名

- [ ] 备份：`mysqldump --single-transaction chatroom > ~/backup/chatroom-$(date +%Y%m%d).sql`（用 `chatroom@127.0.0.1` + `MYSQL_PWD`）。
- [ ] 建库 `chatbox`（utf8mb4/utf8mb4_general_ci）→ 导入备份 → 行数逐表比对（users=556、messages=183、sessions=628 等，以 Task 0 快照为准）。
- [ ] `RENAME USER 'chatroom'@'127.0.0.1' TO 'chatbox'@'127.0.0.1'`（含 localhost 账号；口令不变）；`SHOW GRANTS` 核对。
- [ ] 旧库 `chatroom` 先保留（回滚点），验证通过后再决定删除（写进台账）。

### Task 8: 线上目录/服务/证书/日志切换

- [ ] 同步源码：`~/桌面/0917/chatbox/`（`mv chatroom chatbox` 保 build 缓存 → scp 覆盖 9 个顶层项：CMakeLists.txt、README.md、client、common、conf、deploy、server、tests、tools；`conf/chatbox.env` 由旧 `chatroom.env` 改名 + 变量名替换）。
- [ ] 服务器编译：`cmake --build build -j`（验证改名后的服务端代码在 Linux 编译通过）→ 产出 `build/chatbox_server`。
- [ ] 日志迁移：`logs/chatroom.log` → `logs/chatbox.log`（保历史）。
- [ ] 证书：保留 CA（核对指纹）→ 用现有 `ca.key` 重签服务器证书 `CN=chatbox.local`，SAN `IP:192.168.2.113,DNS:chatbox.local` → `install -m 600` 到 `/etc/chatbox/certs/`（ca.crt/ca.key/chat.crt/chat.key）。
- [ ] nginx：`/etc/nginx/stream.d/chat.conf` 证书路径改 `/etc/chatbox/certs/` → `nginx -t` → `systemctl reload nginx`。
- [ ] systemd：安装 `deploy/chatbox.service` → `systemctl daemon-reload` → `disable --now chatroom` → `enable --now chatbox`。
- [ ] logrotate：装 `deploy/chatbox.logrotate` 为 `/etc/logrotate.d/chatbox`，移除旧文件；`logrotate -d` 干跑校验。

### Task 9: 线上验证与回滚预案

- [ ] `systemctl status chatbox` = active；`ss -ltnp` 见 9100（服务端）与 9443（nginx）。
- [ ] 服务端日志出现 MySQL 连接成功、无 `Access denied`；`stats` 中 db_errors=0。
- [ ] TLS 端到端：本机 `python chatbox\tools\tls_smoke.py`（如脚本支持）或客户端登录实测（9443）。
- [ ] 数据库实测：客户端加载好友/历史消息正常（读的就是 chatbox 库）。
- [ ] 回滚预案（如失败）：`disable --now chatbox` → 恢复旧 unit/nginx/证书目录/DB → `enable --now chatroom`；旧库与备份保留 7 天。

### Task 10: 收尾

- [ ] 台账：`.superpowers/sdd/2026-10-19-rename-to-chatbox/progress.md`（含本次决策：历史文档不重写、SSH 别名保留、旧库/旧证书目录保留）。
- [ ] README 数字与截图索引更新（新增证据图）。
- [ ] 可选：`package_client.ps1` 生成 `dist\chatbox-0.2.0-win64.zip`。
- [ ] 向用户报告：改动清单、验证证据、保留项（旧注册表键/旧库/旧证书目录/SSH 别名/历史文档）。
