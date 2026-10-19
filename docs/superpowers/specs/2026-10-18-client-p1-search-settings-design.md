# 客户端 P1：用户搜索、联系人、日志、设置与分发包

**日期：** 2026-10-18

## 目标

在已完成的消息可靠性/聊天界面基础上，补齐找人、联系人和客户端产品化能力。

## 范围

1. 服务端用户搜索协议与数据库查询
2. 客户端用户搜索对话框
3. 本地联系人列表（QSettings 持久化）
4. 客户端日志文件
5. 设置页（服务器、TLS、通知、日志目录）
6. 应用图标、版本信息、便携 ZIP 分发包

## 非目标

- 图片/文件、引用、@、撤回、消息搜索
- 服务端好友关系表（联系人先存本地）
- 多端同步、离线推送
- 正式安装器（当前环境没有 Inno Setup/NSIS；提供便携 ZIP 和 Inno Setup 脚本模板）

## 协议

新增：

```text
0x0060 USER_SEARCH
请求：keyword（string，1-32 字节）
响应：count u16 + [user_id u64, username, nickname, online u8]
```

## 数据

- 联系人存 `QSettings` 的 `contacts/json`，不写服务端好友表。
- 服务器配置存 `server/host`、`server/port`、`server/tls`。
- 会话 Token 存 `session/*`；退出登录只删除 `session` 组，不能清空联系人和设置。
- 日志文件：`QStandardPaths::AppDataLocation/logs/client.log`，单文件超过 5 MB 轮转为 `client.log.1`。

## 验收

- 搜索用户名/昵称返回用户，双击结果发起私聊并加入联系人。
- 联系人在重启客户端、退出登录后仍存在。
- 设置页修改服务器/TLS/通知后持久化。
- 客户端日志包含连接、登录、错误、发送失败；不包含密码/Token。
- 应用有图标和版本号；`package_client.ps1` 生成便携 ZIP。
- 服务端搜索测试、客户端测试、应用构建通过。

## 实施结果（2026-10-18）

- 服务端 `USER_SEARCH` 已部署，用户名/昵称搜索和空结果测试通过。
- 客户端搜索对话框、双击添加联系人/发起私聊、联系人删除和重启保留通过。
- 退出登录只清 `session`，联系人和服务器/通知设置保留。
- 客户端日志写入 `%APPDATA%\Chatroom\ChatroomClient\logs\client.log`，包含连接、登录、错误、设置保存，不包含密码/Token。
- 版本 0.2.0、应用图标、便携 ZIP 生成并解压运行通过。
- 当前机器没有 Inno Setup/NSIS，仅提供 `installer\chatroom.iss` 模板。
