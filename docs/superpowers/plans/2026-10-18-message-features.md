# 消息功能实施计划与记录

**Spec:** `docs/superpowers/specs/2026-10-18-message-features-design.md`

## 任务

- [x] 扩展 `messages`/`private_messages`：引用快照、recalled。
- [x] 扩展 CHAT_SEND/PRIVATE_SEND、CHAT_PUSH/PRIVATE_PUSH、HISTORY_RESP。
- [x] 新增 MESSAGE_RECALL 0x0070，房间/私聊撤回广播。
- [x] 新增 MESSAGE_SEARCH 0x0080，服务端历史搜索。
- [x] 客户端历史/推送解析引用和撤回状态。
- [x] 客户端引用栏、回复链接、撤回链接。
- [x] 客户端消息搜索对话框。
- [x] @按钮 + 昵称提及提醒。
- [x] 更新 Python 测试工具和 README。

## 验证

- `tools/message_features_client.py`：
  - room quote / recall / search PASS
  - private quote / recall / search PASS
- `tools/private_history_client.py`：全部 PASS
- 服务端 ctest：4/4
- 客户端 QtTest：18 passed
- 客户端手工：引用栏、引用气泡、撤回占位、消息搜索、@按钮
- 应用构建：通过

## 取舍

- @提及首期按昵称文本识别，不建服务端 mention 表；代价是昵称重复/改名可能误判。
- 引用快照由客户端提供，服务端不重新查询原消息；代价是恶意客户端可伪造引用。
- 搜索只返回列表，不定位历史位置。
- 撤回保留引用快照；不清理引用该消息的旧快照。
