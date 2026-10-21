# ChatBox 公网部署设计（SakuraFrp 免费 TCP 隧道）

日期：2026-10-21
状态：已通过（2026-10-21）

## 1. 背景与目标

ChatBox 目前只在学校宿舍局域网内使用：服务端运行在自有 Ubuntu 笔记本
`xxw-XPS-13-9360`（`192.168.2.240`），客户端为 Windows Qt 5.15 程序，客户端通过
`certs/ca.crt` 校验服务端证书，默认连接 `192.168.2.240:9443`。

宿舍路由器为 WTA301，管理后台无法登录，因此不能做端口映射；服务端只有内网 IPv4 和
ULA IPv6，没有可用的全局 IPv6。目标是**不换服务器、不买 VPS、不改路由器**，用
SakuraFrp 免费 TCP 隧道把服务暴露到公网，供朋友/同学使用；注册对任何下载客户端的人
开放。

“企业级”在这里落地为可运维、可恢复、有边界，而不是无限扩容或 99.9% SLA。

## 2. 目标与非目标

### 目标

- 公网用户通过 SakuraFrp 节点域名/端口访问 ChatBox，无需公网 IP、端口映射或域名。
- 保持端到端 TLS：隧道只转发密文；客户端继续用现有本地 CA 校验服务端证书。
- 保持宿舍局域网 `.240:9443` 直连可用；公网和局域网共用同一套服务端与数据库。
- 公网入口拿到真实访客 IP，启用每 IP 注册限流与连接数限制。
- 开放注册，但限制滥用：每 IP 每小时最多 20 次注册请求、50 条并发连接；全局每小时
  最多 100 次注册请求、500 条并发连接（成功建号数只会更少）。共享校园网/CGNAT 出口
  按一个公网 IP 计。
- 服务端进程、nginx、MySQL、隧道客户端随开机启动并自动重启；**不修改 UFW、合盖睡眠
  策略或 Wi-Fi 省电设置**，因为用户上课也要使用这台笔记本。
- 增加每日数据库备份与可验证的恢复步骤。
- 记录免费额度和运行风险，避免把“能连上”误当成“可长期运营”；合盖/睡眠时公网断开是
  明确的限制。

### 非目标

- 不迁移到云 VPS，不购买域名/证书，不做 ICP 备案。
- 不做公网 IPv4 端口映射、UPnP、路由器重置或学校网络改造。
- 不做邮箱/手机验证、验证码、邀请码、管理后台或内容审核系统。
- 不做多机水平扩展、负载均衡、跨设备消息同步或 99.9% SLA。
- 不改变 ChatBox 的二进制协议、消息格式或客户端 TLS 校验模型。
- 不把 SakuraFrp 的访问认证（浏览器密码/TOTP）强加给 ChatBox 客户端；那会破坏现有协议。

## 3. 现状与约束

| 项目 | 现状 |
| --- | --- |
| 服务端 | Ubuntu，`192.168.2.240/24`，Wi-Fi 网卡 `wlp58s0`，网关 `192.168.2.1` |
| 出口 | 公网 IPv4 出口 `112.101.115.241`（不使用；是否有固定 IP/上游 NAT 未确认） |
| IPv6 | 仅 ULA `fd74:...`，无全局 IPv6 |
| 服务 | `chatbox.service` 监听 `127.0.0.1:9100`，带 `--proxy-protocol` |
| TLS 入口 | nginx stream `0.0.0.0:9443 ssl` 终止 TLS，转发到 `127.0.0.1:9100` |
| 证书 | 本地 CA `ChatBox Dev CA`；服务端证书 SAN 仅 `192.168.2.240`、`chatbox.local` |
| 客户端 | 使用 `certs/ca.crt` 校验证书并校验主机名；默认 `.240:9443` |
| MySQL | **8.4.11**，仅 `127.0.0.1:3306`；业务账号 `chatbox` 只有 DML 权限；本机 root socket 登录可用 |
| SSH | 仅密钥登录（`PasswordAuthentication no`、`PermitRootLogin prohibit-password`） |
| 管理来源 | Windows 管理机 `192.168.2.117`，与服务端同网段；不依赖 Radmin VPN |
| nginx | 1.28.3；有 `stream_ssl`/`stream_realip`，**没有** `stream_limit_conn` 模块 |
| 防火墙 | `ufw` 未启用；`22/80/9443` 在局域网可访问 |
| 磁盘 | 根分区 46G，已用 29G，可用 16G |
| 免费额度 | SakuraFrp 免费约 5 GiB/月、10 Mbps；超额/限速会影响服务 |

运行约束：

- 服务器是用户上课也要使用的宿舍笔记本；保留现有 UFW、合盖、睡眠和 Wi-Fi 省电设置。
  只保证服务随系统启动；合盖/睡眠、断网、断电时公网不可用，不承诺 24 小时在线。
- SakuraFrp 免费节点、域名和远程端口由平台分配；节点变更时公网地址可能变化。
- 现有 CA 不能重新生成，否则所有客户端都要重新分发；只轮换服务端证书。
- 服务器上的服务端源码在 `/home/xxw/桌面/0917/chatbox`，通过 SSH/scp 部署；代码仓库在 Windows 工作区。
- 任何公网暴露都会带来扫描、撞库、批量注册和流量消耗；开放注册必须配合限流。

## 4. 方案总览

```text
公网客户端
  -> frp-oil.com:15318                (SakuraFrp TCP 节点)
  -> frpc（宿舍服务器主动外连，token/tunnel）
  -> 127.0.0.1:9444  nginx stream（TLS 终止 + 接收 PROXY v2）
  -> 127.0.0.1:9100  chatbox（--proxy-protocol）
  -> 127.0.0.1:3306  MySQL

宿舍局域网客户端
  -> 192.168.2.240:9443  nginx stream（TLS 终止，原有路径）
  -> 127.0.0.1:9100      chatbox
```

公网和局域网入口使用同一张服务端证书；证书 SAN 增加 Sakura 节点域名，同时保留
`192.168.2.240` 和 `chatbox.local`。客户端 `ca.crt` 不变。

## 5. 关键设计决定

### 5.1 SakuraFrp 隧道

- 隧道类型：**TCP**。
- 本地目标：`127.0.0.1:9444`（专供隧道，和局域网 9443 分离）。
- 节点：用户已选择节点，公网端点为 **`frp-oil.com:15318`**（节点 `frp-oil.com`，
  远程端口 `15318`）。
- 高级设置：在 SakuraFrp **用户资料页**开启“高级用户模式”，然后在隧道编辑页的
  “自定义设置”填入 `proxy_protocol_version = v2`，让 frpc 在转发给 nginx 前附加
  PROXY v2 头。已确认保存成功。
- 客户端：使用 SakuraFrp 官方分发的 `frpc`（要求版本号包含 `sakura`，建议
  `0.51.0-sakura-14` 或更新），首次用 `-f <token>:<tunnel_id> -w` 拉取配置，生成
  `/etc/chatbox/frpc.ini`（root 600）；systemd 只执行 `frpc -c
  /etc/chatbox/frpc.ini`，token 不进入进程参数、不进入 unit 文件、不写日志。
- 设置阶段的 token 只放在 root 600 的临时 env/配置中，生成配置后删除临时文件；不在
  聊天、仓库或命令行历史中留下 token。
- 不用 SakuraFrp 的浏览器访问认证，因为 ChatBox 客户端不会完成网页认证流程；访问控制
  依赖 ChatBox 登录/注册、TLS 和每 IP 限流。

**硬前提**：SakuraFrp 面板必须允许该隧道启用高级模式并设置
`proxy_protocol_version = v2`。如果免费套餐不允许，公网入口拿不到真实 IP，每 IP 限流
会退化成所有用户共享 `127.0.0.1`，不能按本设计上线；此时应改用 VPS 或重新评估方案。

### 5.2 nginx 双入口

保留现有：

```nginx
server {
    listen 9443 ssl;
    ...
    proxy_pass chat_backend;
    proxy_protocol on;
}
```

新增仅本机可连的隧道入口：

```nginx
server {
    listen 127.0.0.1:9444 ssl proxy_protocol;
    ssl_certificate     /etc/chatbox/certs/chat.crt;
    ssl_certificate_key /etc/chatbox/certs/chat.key;
    ssl_protocols TLSv1.2 TLSv1.3;
    proxy_pass chat_backend;
    proxy_protocol on;
}
```

- `9444` 只绑定 loopback，公网无法直接连；只有本机 frpc 能访问。
- `proxy_protocol` 让 nginx 接收 frpc 的 PROXY v2 头，并把真实客户端地址继续传给
  chatbox。
- `9443` 不加 `proxy_protocol` 监听参数，局域网客户端不需要改协议。
- 为 stream 增加访问日志，记录 `$remote_addr` 和 `$proxy_protocol_addr`，用于验证真实 IP；
  日志只记录连接元数据，不记录消息正文。

### 5.3 证书

- **不删除、不重建 CA**；复用 `/etc/chatbox/certs/ca.crt` 和 `ca.key`。
- 扩展 `deploy/gen_certs.sh`，让 `SAN_DNS` 支持逗号分隔的多个 DNS，生成：
  - `IP:192.168.2.240`
  - `DNS:chatbox.local`
  - `DNS:frp-oil.com`
- 重新签发 `chat.crt` 后 `nginx -t && systemctl reload nginx`。
- 用 `openssl s_client -connect frp-oil.com:15318 -servername frp-oil.com
  -CAfile ca.crt` 验证 SAN 和链。
- 节点域名变化时只重签服务端证书并更新客户端连接地址；不重建 CA。

### 5.4 客户端

- 不修改 TLS 校验逻辑，不绕过主机名校验。
- 客户端包中的 `certs/ca.crt` 保持不变；旧客户端可以连新证书。
- 公网用户首次使用时在登录页填写 `frp-oil.com` 和 `15318`，勾选 TLS；
  QSettings 会记住。
- 默认地址仍保留 `.240:9443`，避免破坏宿舍局域网使用。
- 文档写清楚：公网地址只在朋友/同学之间分发；客户端包必须带 `certs/ca.crt`。

### 5.5 注册与连接限流

现有 `LoginRateLimiter` 只限制登录失败，注册没有频率限制；systemd 当前
`--max-conn-per-ip 0` 关闭了单 IP 连接限制。

**注册限流**：

- 新增 `RegistrationRateLimiter`，在 WorkerPool 中共享、线程安全，按 1 小时窗口统计：
  - 每 IP 最多 20 次通过格式校验的注册请求；
  - 全局最多 100 次。
- 计数点在 **格式校验之后、密码哈希/数据库写入之前**。重复用户名、数据库错误也会消耗
  配额；这样恶意请求不能免费触发 Argon2id 哈希或建号。被拒绝返回
  `1005 too many registrations`，并增加 `rate_limited` 指标。
- 窗口从该 IP 的首次请求开始计时；进程重启会清空计数（内存限流，不持久化）。窗口
  过期后重新允许；不把 IP 限流宣传成完整反滥用。
- 单元测试覆盖：第 21 次同 IP 请求被拒、另一个 IP 仍可注册、全局第 101 次被拒。

**每 IP 连接数**：

- 不能用现有 `ConnectionLimiter` 的 accept 阶段地址：经 nginx 连接时该地址永远是
  `127.0.0.1`，会把所有用户合并成同一个 IP 限额。
- 也**不能**用 nginx `stream` 的 `limit_conn`：当前 nginx 没有
  `stream_limit_conn` 模块，不能假设可安装。
- 修改服务端限流：当连接使用 PROXY protocol 时，accept 阶段只做全局计数；解析完
  PROXY 头拿到真实 IP 后，调用 `rekey` 把该连接计入真实 IP。若真实 IP 已达到
  50 条，则关闭该连接；连接关闭时按真实 IP 释放。局域网 9443 和公网 9444 都经过
  nginx，因此都走同一套后置计数。
- 连接上限：全局 **500**，每 IP **50**（`--max-connections 500
  --max-conn-per-ip 50`）。原先的 `--max-conn-per-ip 0` 必须移除。
- 若 PROXY v2 头解析失败或 SakuraFrp 不支持真实 IP，公网入口不得启用每 IP 限流；
  这属于上线阻断条件，不能静默退化成 127.0.0.1 共享配额。
- 单元测试覆盖：延迟计数、rekey 到真实 IP、超过 50 拒绝、关闭后释放、LAN 直连不受
  公网配额影响。

现有消息 token bucket、登录失败限流、全局连接限流继续保留。开放注册不要求邮箱/手机。

### 5.6 主机防火墙：保持现状

用户明确要求不改这台上课用笔记本的系统设置，因此**不启用、不修改 `ufw`**。

边界与补偿措施：

- 公网入口只经过 SakuraFrp 隧道；`9444` 只监听 loopback，节点无法直接访问 `9443/22`。
- 路由器没有做端口映射；公网不能直接连 `22/9100/3306/9443/9444`。
- SSH 仍仅密钥登录（`PasswordAuthentication no`），管理机 `192.168.2.117` 在局域网。
- `9443` 继续对局域网开放；这是现有行为，不是本次新增的公网入口。
- 残余风险：局域网内其他设备可以访问 `9443`/`22`；没有主机防火墙隔离。后续如用户同意，
  再单独评估 UFW 规则，不在本次部署中修改。

### 5.7 每日备份

- 服务器实际是 **MySQL 8.4.11**；项目规则里的 5.7 兼容写法继续保留，但部署命令按
  8.4 执行。业务账号 `chatbox` 只有 DML 权限，不能用于 `mysqldump`/恢复。
- 通过本机 root socket 登录创建专用备份账号 `chatbox_backup@localhost`，只授予
  `SELECT, LOCK TABLES, SHOW VIEW, EVENT, TRIGGER ON chatbox.*`；恢复演练用 root
  socket 创建/删除临时库，不给备份账号全局建库权限。
- `/usr/local/sbin/chatbox-backup.sh`：
  `mysqldump --single-transaction --quick --no-tablespaces --set-gtid-purged=OFF
  --routines --triggers`，gzip 到 `/var/backups/chatbox/`，保留 7 天。
  `--no-tablespaces` 避免 8.4 下的全局 PROCESS 权限要求。
- DB 凭据放 `/etc/chatbox/backup.cnf`，`chmod 600`，不写入仓库/日志；脚本用
  `--defaults-extra-file`。
- `chatbox-backup.service` + `chatbox-backup.timer`，每天 03:15 运行；持久化定时器
  （`Persistent=true`）补跑错过的任务。服务以 root 运行，脚本检查 `mysqldump` 退出码
  并保留最近一份成功备份。
- 恢复演练：`sudo mysql` 创建 `chatbox_restore_test`，把最近备份导入临时库，核对表
  数量/关键行数，再删除临时库；不在生产库上直接恢复。
- 备份只在本机；宿舍断电/磁盘损坏仍可能同时丢失生产库和备份，后续可加异机/网盘副本。

### 5.8 开机自启与可用性边界

- **只做** `chatbox`、`nginx`、`mysql`、`sakurafrp` 的 `systemctl enable`；服务随系统
  启动，`chatbox`/`sakurafrp` 配置自动重启。
- **不改** `systemd-logind` 合盖策略，不 mask 睡眠 target，不改 NetworkManager/Wi-Fi
  省电。用户上课使用笔记本时，合盖或系统睡眠会导致公网隧道断开。
- 不承诺 24 小时在线；公网可用窗口 = 笔记本开机、醒着、联网且 SakuraFrp 节点正常的
  时间段。局域网在笔记本醒着时同样可用。
- 断电/网络故障后需要人工开机/恢复；这不是高可用方案。

### 5.9 运维与观测

- `systemctl status chatbox nginx mysql sakurafrp`、`journalctl -u sakurafrp`。
- nginx stream 访问日志、ChatBox 日志、`ss -ltnp`、`df -h`；UFW 保持未启用，不用它做验收。
- 每周检查 SakuraFrp 面板的流量用量（免费 5 GiB/月）；接近上限时提前决定限流/升级/停服。
- 免费节点/端口变化时更新客户端文档并重签证书。
- 不在日志中打印 token、密码、消息正文或 CA 私钥。

## 6. 安全与隐私边界

- TLS 在 nginx 终止；SakuraFrp 节点只看到 TLS 密文和连接元数据，看不到消息正文。
- CA 私钥只存在服务器 `/etc/chatbox/certs`（root 600）；客户端只分发 `ca.crt`。
- 公网只暴露 Sakura 隧道对应的 ChatBox TLS 端口；SSH、MySQL、9100、9444 不对公网开放。
- 注册开放，但每 IP 限流；没有邮箱验证，无法完全阻止分布式批量注册。若出现滥用，按
  IP/账号在数据库/防火墙层面处置，后续再考虑验证码或邀请码。
- 免费隧道没有 SLA；节点维护、限速、流量耗尽都属于可接受故障模式，需在用户文档中写明。
- 备份包含用户数据，权限 root-only；恢复测试使用临时库，避免污染生产。

## 7. 失败模式与回滚

| 故障 | 处理 |
| --- | --- |
| frpc 未启动/token 失效 | `systemctl status/journalctl -u sakurafrp`；更新 `/etc/chatbox/sakurafrp.env` 后重启 |
| 节点域名/端口变化 | 面板确认新地址；重签证书 SAN；更新客户端文档；重启 frpc/nginx |
| 证书 SAN 不匹配 | `openssl x509 -text` 检查；用同一 CA 重签；不要重建 CA |
| nginx 9444 配置错误 | `nginx -t` 失败则不 reload；恢复上一份配置 |
| 合盖/睡眠 | 公网隧道断开是预期行为；唤醒笔记本后 frpc/ssh 自动重连，必要时重启服务 |
| 宿舍断电/断网 | 恢复供电/网络后等待服务自启；必要时人工开机/重启；这不是高可用方案 |
| 免费流量耗尽 | 面板确认；暂停公网或升级套餐；保留局域网使用 |
| 注册滥用 | 先观察限流指标；必要时临时关闭注册入口或按 IP/账号处置 |
| 需要撤销公网 | `systemctl disable --now sakurafrp`；删除/注释 nginx 9444；reload；局域网仍可用 |

代码/服务端变更回滚：保留旧二进制和配置备份；注册限流是独立模块，可按提交回退并重启
`chatbox`，不影响数据库 schema。

## 8. 验收标准

1. 公网：从宿舍外网络（手机热点或外部主机）用客户端连接
   `frp-oil.com:15318`，TLS 校验通过，能登录/注册/收发消息。
2. 局域网：`192.168.2.240:9443` 仍能连接，行为不变。
3. 证书：`openssl s_client` 显示 SAN 包含节点域名，链由 `ChatBox Dev CA` 签发；客户端不
   出现证书错误。
4. 真实 IP：nginx stream 日志中的公网连接来源不是 `127.0.0.1`；chatbox 日志/限流按真实
   IP 生效。
5. 限流：同一 IP 第 21 次注册在 1 小时窗口内被拒绝；该 IP 第 51 条并发连接被拒绝；
   另一个 IP 仍可正常注册/连接；全局第 101 次注册被拒绝；正常用户不受影响。
6. 网络边界：UFW 保持未启用/原状态；`9444` 仅 loopback，公网只能经 Sakura 隧道；
   公网无法直连 22/9100/3306/9443/9444；未新增路由器端口映射。
7. 备份：专用 `chatbox_backup` 账号可用；timer 已启用；手动运行生成可解压的 dump；
   用 root socket 在 `chatbox_restore_test` 恢复成功；保留策略生效。
8. 重启：服务器重启且笔记本醒着时，`chatbox/nginx/mysql/sakurafrp` 自动起来；公网和
   局域网恢复。合盖/睡眠时不恢复公网，这是明确接受的限制。
9. 醒着窗口：笔记本保持醒着的 2 小时窗口内隧道在线、无异常退出、无磁盘/内存耗尽；
   记录断线次数。不做 24 小时常开验收。
10. 额度：SakuraFrp 面板显示流量/限速状态；文档写明超额后的行为。

## 9. 实施前置条件（已确认/待放置）

已确认：

- TCP 隧道已创建，本地目标 `127.0.0.1:9444`；公网端点 `frp-oil.com:15318`。
- 已开启高级用户模式，隧道自定义设置 `proxy_protocol_version = v2` 已保存。
- 隧道访问密码和 TOTP 均留空（未启用访问认证）。
- 设计已通过评审。
- 用户明确：只做服务开机自启，**不修改 UFW、合盖睡眠和 Wi-Fi 省电**；接受合盖/睡眠时
  公网断开。

待放置：

- token 和隧道 ID：**不要发在聊天里**；实施时由用户在服务器上写入 root 600 的临时
  文件，生成 `/etc/chatbox/frpc.ini` 后删除临时文件。
- 用户上课也要使用笔记本：不修改 UFW、合盖睡眠和 Wi-Fi 省电；接受“合盖/睡眠时公网
  断开、只在笔记本醒着时可用”。
- 确认公网地址只发给朋友/同学；客户端包会带本地 CA。

## 10. 构建/部署边界

- 服务端新增 `RegistrationRateLimiter` 及单元测试；其余是 nginx/systemd/备份脚本/证书
  配置，不改协议、不改 MySQL schema、不修改 UFW/电源/Wi-Fi 设置。
- 客户端不重新编译；复用现有 portable ZIP，只需在文档中写公网地址，并让用户首次输入。
- 服务器端通过 SSH/scp 部署；不在服务器上拉取 GitHub，不使用 FTP/SMB。
- 所有配置/脚本进入代码仓库（token、DB 密码、私钥除外）；部署后提交。
- 免费方案的额度/节点稳定性是外部依赖，不能通过代码消除。

## 11. 已确认与剩余运行假设

已确认：

1. SakuraFrp 高级用户模式可用，`proxy_protocol_version = v2` 已保存。
2. 公网端点 `frp-oil.com:15318`；token 和隧道 ID 通过 root 600 文件放置，不进聊天。
3. 设计已通过。
4. 只做服务开机自启；UFW、合盖睡眠、Wi-Fi 省电保持原状，睡眠时公网断开。

剩余运行假设（由运维承担，不阻塞实施）：

1. 笔记本保留现有合盖/睡眠/Wi-Fi/UFW 设置；合盖或睡眠时公网断开，唤醒后自动重连；
   断电/断网由人工恢复。
2. 公网客户端分发当前 portable ZIP，用户首次手动填写节点地址/端口。
3. 免费额度（约 5 GiB/月、10 Mbps）用完后公网不可用，局域网仍可用。
