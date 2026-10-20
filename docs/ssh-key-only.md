# SSH ed25519 密钥登录配置

> 目标主机：`xxw@192.168.2.113`（Ubuntu 26.04）
> 实施日期：2026-10-18
> 地址更新（2026-10-20）：服务器已固定为 `192.168.2.240`，SSH 别名 `chatroom-dev` 指向新地址。
> 结果：密码登录已关闭，仅允许 ed25519 公钥登录。

## 1. 本机密钥

Windows 本机已有密钥：

```text
C:\Users\30761\.ssh\id_ed25519
C:\Users\30761\.ssh\id_ed25519.pub
```

指纹：

```text
SHA256:qhHx7STYNHMCzpl8Q3TAHVjs87lZfTK7l4TwqTgd728 desktop-to-laptop
```

公钥已加入远程：

```text
/home/xxw/.ssh/authorized_keys
```

如换一台新电脑，生成并上传新密钥：

```powershell
ssh-keygen -t ed25519 -a 100 -C "your-name@your-pc"
# 将 id_ed25519.pub 内容追加到远程 ~/.ssh/authorized_keys
```

```bash
mkdir -p ~/.ssh
chmod 700 ~/.ssh
printf '%s\n' 'ssh-ed25519 AAAA... your-name@your-pc' >> ~/.ssh/authorized_keys
chmod 600 ~/.ssh/authorized_keys
```

## 2. 服务端配置

新增 drop-in 文件：

```text
/etc/ssh/sshd_config.d/99-chatroom-key-only.conf
```

内容：

```text
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
AuthenticationMethods publickey
```

生效流程：

```bash
sudo cp -a /etc/ssh/sshd_config /etc/ssh/sshd_config.bak.$(date +%Y%m%d%H%M%S)
sudo install -m 0644 -o root -g root \
  99-chatroom-key-only.conf \
  /etc/ssh/sshd_config.d/99-chatroom-key-only.conf
sudo sshd -t                       # 必须先校验通过
sudo systemctl reload ssh          # 再 reload
```

本次备份文件：

```text
/etc/ssh/sshd_config.bak.20261018162856
```

## 3. 本机 SSH 配置

`C:\Users\30761\.ssh\config` 已增加：

```text
Host chatroom-dev
  HostName 192.168.2.240
  User xxw
  IdentityFile ~/.ssh/id_ed25519
  IdentitiesOnly yes
  ServerAliveInterval 30
```

之后直接：

```powershell
ssh chatroom-dev
```

本机配置备份：

```text
C:\Users\30761\.ssh\config.bak.20261018162914   # 加入主密钥别名前
C:\Users\30761\.ssh\config.bak.20261018163054   # 加入备用密钥别名前
```

## 4. 验证

密钥登录：

```powershell
ssh -o BatchMode=yes chatroom-dev "echo key-ok"
```

密码认证应被拒绝：

```powershell
ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no `
    -o BatchMode=yes xxw@192.168.2.240 "echo should-fail"
```

预期：

```text
Permission denied (publickey).
```

服务端有效配置：

```bash
sudo sshd -T | grep -E 'passwordauthentication|authenticationmethods'
# passwordauthentication no
# authenticationmethods publickey
```

## 5. 回滚

如果密钥丢失或需要临时恢复密码登录：

```bash
sudo rm -f /etc/ssh/sshd_config.d/99-chatroom-key-only.conf
sudo cp -a /etc/ssh/sshd_config.bak.20261018162856 /etc/ssh/sshd_config
sudo sshd -t
sudo systemctl reload ssh
```

> 回滚后密码登录恢复，但建议仅在应急时使用，并尽快重新配置密钥。

## 6. 备用密钥

已新增一把独立的 ed25519 备用密钥：

```text
C:\Users\30761\.ssh\id_ed25519_backup
C:\Users\30761\.ssh\id_ed25519_backup.pub
```

指纹：

```text
SHA256:3WW9fR2+kjRDcuTaH6thYCmD9FIHyxk/OG7MuTgear8 backup-key-20261018
```

远程 `authorized_keys` 现在包含两把公钥：

```text
desktop-to-laptop      （主密钥）
backup-key-20261018    （备用密钥）
```

本机新增别名：

```powershell
ssh chatroom-dev-backup
```

> 重要：备用密钥目前和主密钥在同一台 Windows 上，无法防止整机丢失或磁盘损坏。
> 请把 `id_ed25519_backup` 复制到 U 盘、密码管理器或离线介质。复制到新电脑后放到
> `~/.ssh/id_ed25519_backup`，Linux/macOS 执行 `chmod 600`，Windows 确保只有当前用户可读。

建议给备用密钥加口令：

```powershell
ssh-keygen -p -f $env:USERPROFILE\.ssh\id_ed25519_backup
```

## 7. 建议

当前主密钥和备用密钥均未设置口令，便于自动化。若要更高安全性：

```powershell
ssh-keygen -p -f $env:USERPROFILE\.ssh\id_ed25519
```

然后在 Windows 上启用 `ssh-agent` 并 `ssh-add`，避免每次输入口令：

```powershell
Set-Service -Name ssh-agent -StartupType Automatic
Start-Service ssh-agent
ssh-add $env:USERPROFILE\.ssh\id_ed25519
```
