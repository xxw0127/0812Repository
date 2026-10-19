# SDD ledger — plan: docs/superpowers/plans/2026-10-19-rename-to-chatbox.md

Task list: Task 0 准备 / Task 1 仓库结构改名 / Task 2 内容替换 / Task 3 设置迁移(TDD) /
Task 4 文档 / Task 5 本地验证 / Task 6 提交推送 / Task 7 线上数据库 / Task 8 线上切换 /
Task 9 线上验证 / Task 10 收尾.

Pre-flight: 任务间为顺序依赖（Task 1 产物目录被 Task 2/3/5 消费；Task 2 的服务端默认值被 Task 7/8 消费；
Task 3 的迁移被 Task 5 验证；Task 8 依赖 Task 2 的 deploy 文件）。无并行冲突。

Ruling: 在主工作树（branch rename-to-chatbox）执行而非 git worktree —— 交付物本身包含"用户实际使用的物理目录改名"
（chatbox/client/build/release/chatbox.exe 即用户运行路径），worktree 无法交付；成本：若出错靠 git 历史回滚。

Task 0: complete（停两个客户端实例；分支 rename-to-chatbox；服务器备份：库 dump 265KB + 行快照 + 证书/unit/nginx/logrotate 副本）

Task 1: Ruling: `git mv chatroom chatbox` 被 Windows 拒绝（未知进程持有 client\build 的目录句柄；Restart Manager、全量 CWD 扫描、
模块扫描、ACL 对比、优雅关闭 Qt Creator 均未定位）→ 改为「新建 chatbox/ + 逐项 Move-Item，把被占用的 client\build 留在旧路径」；
收尾时锁自行解除，旧目录已整体删除 —— 代价：多一步手工搬运（已完成，工作区已清理）。
Task 1: complete（结构改名提交 0313963：305 项纯重命名；.gitignore 同步新路径）

Task 2: complete（内容替换 124 文件 + 4 个文件 git mv 改名；白名单外残留 = 0；
TARGET=chatbox → exe=chatbox.exe；包名 chatbox-0.2.0；app.rc InternalName/OriginalFilename=chatbox(.exe)；服务端默认 db_user/db_name=chatbox、logs/chatbox.log、CHATBOX_DB_PASSWORD）

Task 3: complete（TDD：先 RED —— make 报 `No rule to make target '../core/app_settings.cpp'`；实现 core/app_settings.{h,cpp} + main.cpp 接线 → GREEN 44 passed / 0 failed；新增 settingsMigrationCopiesLegacyStore、settingsMigrationNeverOverwritesCurrent、brandIdentifiersAreUnified）

Task 4: Ruling: docs/2026-10-18-linux-chatroom-design.md 与 docs/ssh-key-only.md 不改名、不改内容 —— 前者是带日期的历史规划书（其路径/用户命名本就不等于实际部署，实际部署由 chatbox/README.md 描述），后者是 SSH 运维记录；SSH 别名 chatroom-dev 保留 —— 代价：仓库内仍有 2 份历史文档含旧名（如需可后续一条命令刷）。
Task 4: complete（根 README 重写为 ChatBox 项目说明；服务端/客户端 README 更新路径、exe 名、包名、日志目录、迁移说明、验证记录与证据图索引）

Task 5: complete（44/44；release 全新重建 → chatbox.exe；启动验证：窗口标题 ChatBox、日志 `settings migrated from the pre-ChatBox store`、RESUME user_id=494 自动登录、注册表 `HKCU\Software\ChatBox\ChatBox` 与旧键并存；证据图 chatbox/client/screenshot-chatbox-rename.png）

Task 7: complete（cutover dump 265233 字节；9 表逐表行数新旧一致；RENAME USER → chatbox@127.0.0.1/localhost；旧库 chatroom 保留为回滚点）

Task 8: complete（源码 tar 同步 → 服务器 `cmake --build` 通过 + CTest 5/5；目录 mv → ~/桌面/0917/chatbox；证书保留 CA 重签 CN=chatbox.local 到 /etc/chatbox/certs；nginx 证书路径切换 + reload；systemd chatbox.service enabled+active；logrotate 切换；停机 22:56:11 → 22:56:34 = 23 秒）
Task 8: Ruling: tar 从 Windows 打包不保留可执行位，切换后首次启动报 status=203/EXEC → `chmod +x tools/run_server.sh deploy/*.sh` 后重启正常 —— 代价：多一次 20 秒级重启（已记入停机时间）。

Task 9: complete（CA sha256 新旧目录完全一致 → 客户端 ca.crt 无需更新；`openssl verify` 链校验 OK；9102 临时实例 `auth_client.py` ALL AUTH CHECKS PASSED（注册/重复注册/错误口令/登录/踢下线/恢复/登出，写入改名后的 chatbox 库、无 ERROR）；生产客户端 23 秒后自动重连 RESUME 成功；新路径重建 CTest 5/5；9100/9443 监听正常；回滚点齐备）

Task 9: Ruling: `tools/tls_smoke.py` 在本机 Python 3.14 下报 `CA cert does not include key usage extension`（CERTIFICATE_VERIFY_FAILED）—— 与本改动无关（CA 字节未变、Qt 客户端 TLS 校验通过、openssl verify 通过），改用 openssl verify + auth_client.py 作为证据 —— 代价：README 中的 tls_smoke 命令在该 Python 版本下会失败（历史遗留，未修）。

Task 10: 进行中（详见收尾记录）

