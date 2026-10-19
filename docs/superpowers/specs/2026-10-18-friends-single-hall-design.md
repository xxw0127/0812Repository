# 好友系统与单大厅改造

**日期：** 2026-10-18

## 目标

把“多房间大厅”收敛为一个公共大厅，左侧主列表改成服务器好友；私聊入口以好友为主。

## 好友规则

- 搜索用户后发送好友申请。
- 对方在线时实时收到申请提醒；离线时登录后拉取待处理申请。
- 只能接受/拒绝发给自己的申请。
- 接受后建立双向好友关系。
- 好友列表显示在线状态；私聊未读和最后消息合并到好友行。
- 支持删除好友。
- 支持好友备注：按 owner + friend 保存在服务端，只有本人可见；留空清除。
- 每个用户有不可变身份标识 `U + 8 位 user_id`，可用于搜索和展示。
- 不允许加自己、重复申请、已是好友再申请。

## 协议

```text
0x0090 FRIEND_REQUEST        target_id u64
0x0091 FRIEND_RESPOND        requester_id u64, accept u8
0x0092 FRIEND_LIST           空
0x0093 FRIEND_REQUESTS       空
0x0094 FRIEND_REMOVE         friend_id u64
0x0095 FRIEND_REQUEST_PUSH   from_id u64, username, nickname
0x0096 FRIEND_ACCEPT_PUSH    friend_id u64, username, nickname, online u8
0x0097 FRIEND_SET_REMARK     friend_id u64, remark
```

`FRIEND_LIST` 响应：
`count u16 + [user_id u64, username, nickname, online u8, remark]`
`FRIEND_REQUESTS` 响应：
`count u16 + [requester_id u64, username, nickname, created_at]`

## 数据库

`friend_requests(requester_id,target_id,status,created_at,updated_at)`
`friendships(user_low,user_high,created_at)`
`friend_remarks(owner_id,friend_id,remark,updated_at)`

## 单大厅

- 服务端房间数据保留，避免删历史消息。
- 客户端只显示第一个房间，标题改为“大厅”。
- 不再显示房间列表的 2-9 等条目。

## 验收

- 搜索用户 → 发申请 → 对方接受 → 双方好友列表出现。
- 好友请求推送、离线后拉取。
- 好友双击私聊，未读/最后消息显示在好友行。
- 好友备注只对设置者可见，重登/换设备仍保留。
- 用户ID可搜索，展示格式为 `U00000278`。
- 删除好友。
- 左侧只显示一个大厅。
- 服务端 ctest、客户端测试、应用构建通过。

## 实施结果（2026-10-18）

- 好友申请/推送/接受/双方列表/删除通过 `tools/friend_client.py`。
- 客户端单大厅、好友申请角标、申请对话框、接受后好友列表和私聊入口手工通过。
- 好友列表已改为在线/离线分组、两行名称+消息预览、时间/未读角标。
- 好友备注已改为服务端 `friend_remarks` 存储，跨设备同步，好友本人不可见。
- 用户身份标识固定为 `U + 8 位 user_id`，设置页、搜索、好友申请和好友提示中展示；搜索支持 ID。
- 服务端仍保留其他大厅数据，客户端只显示第一个大厅。
- 好友列表只显示服务端好友；本地联系人不再作为主列表。
- 客户端 19 个测试通过，应用构建成功。
