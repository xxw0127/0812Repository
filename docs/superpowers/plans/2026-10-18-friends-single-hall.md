# 好友系统与单大厅实施记录

**Spec:** `docs/superpowers/specs/2026-10-18-friends-single-hall-design.md`

## 完成

- 新增 `friend_requests` / `friendships` / `friend_remarks` 表和迁移脚本。
- 新增好友协议 0x0090-0x0097。
- 服务端 `FriendService`：申请、接受/拒绝、列表、待处理申请、删除、备注。
- 在线申请推送、接受推送。
- 客户端：
  - 左栏只显示一个“大厅”。
  - “好友”列表只显示服务器好友。
  - 好友行合并私聊未读/最后消息，在线/离线分组。
  - 好友备注通过服务端保存。
  - 显示 `U00000278` 格式身份标识；搜索支持 ID。
  - “好友申请 (N)”按钮和申请对话框。
  - 搜索用户双击发送好友申请。
  - 右键好友可设置备注或删除。
- 私聊历史/消息功能保持不变。

## 验证

- 服务端 ctest：4/4
- 客户端 QtTest：19 passed
- `tools/friend_client.py`：申请、推送、申请列表、接受、双方好友列表、备注所有者隔离、清除备注、公开ID搜索、删除全部 PASS
- 客户端手工：单大厅、好友申请角标、申请对话框、接受后好友列表出现、好友私聊入口
- 好友列表两行布局、在线/离线分组、服务端备注显示
- 应用构建：通过

截图：

- `client/screenshot-final-server-remark.png`
- `client/screenshot-friend-menu2.png`
- `client/screenshot-final-friends-id.png`
- `client/screenshot-server-remark-ui.png`
- `client/screenshot-friends-two-line.png`
- `client/screenshot-friends-final2.png`
- `client/screenshot-friends-empty.png`
- `client/screenshot-friend-request.png`
- `client/screenshot-friend-accepted.png`
- `client/screenshot-friends-final.png`

## 取舍

- 服务端仍保留大厅 2-12 的数据，客户端只显示第一个大厅，避免误删历史消息。
- 好友列表只显示服务器好友；非好友私聊不再进入左侧好友列表。
- 本地联系人 QSettings 代码保留但不再作为主 UI，好友关系和备注以服务端为准。
- 用户身份标识直接使用不可变数据库主键，不额外生成随机号，保证唯一且无碰撞。
