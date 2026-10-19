# SDD ledger — plan: docs/superpowers/plans/2026-10-19-rename-to-chatbox.md

Task list: Task 0 准备 / Task 1 仓库结构改名 / Task 2 内容替换 / Task 3 设置迁移(TDD) /
Task 4 文档 / Task 5 本地验证 / Task 6 提交推送 / Task 7 线上数据库 / Task 8 线上切换 /
Task 9 线上验证 / Task 10 收尾.

Pre-flight: 任务间为顺序依赖（Task 1 产物目录被 Task 2/3/5 消费；Task 2 的服务端默认值被 Task 7/8 消费；
Task 3 的迁移被 Task 5 验证；Task 8 依赖 Task 2 的 deploy 文件）。无并行冲突。

Ruling: 在主工作树（branch rename-to-chatbox）执行而非 git worktree —— 交付物本身包含"用户实际使用的物理目录改名"
（chatbox/client/build/release/chatbox.exe 即用户运行路径），worktree 无法交付；成本：若出错靠 git 历史回滚。
