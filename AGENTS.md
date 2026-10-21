---
title: C++跨平台开发规范（Qt客户端 + 服务端Linux/Windows + Socket + MySQL5.7）
description: 项目开发、代码编写、问题排查、代码评审统一规则，所有C++相关输出必须遵守本规则
---
# C++跨平台项目开发规则
> 适用场景：C++项目，**客户端为 Qt C++ 桌面程序；服务端默认运行在 Linux，也可部署为 Windows 原生 C++ 服务程序**；通信层使用原生 Socket，数据库使用 MySQL5.7。所有代码、方案、调试分析、代码重构都遵循下面全部约束。

## 0. 部署架构与角色约束
1. **客户端**：Qt C++ 桌面程序（Windows 为主，可选 Linux），UI 与业务分离；网络通信用客户端 Socket。
2. **服务端**：
    - 默认目标平台：**Linux（ Ubuntu 20.04+）**，守护进程运行；
    - 可选部署目标：**Windows 原生 C++ 服务程序**（可注册为 Windows Service）；
3. **通信协议**：客户端 ↔ 服务端使用 TCP 长连接；协议字段必须跨平台字节序一致（网络大端），结构体禁止直接收发，必须序列化。
4. **数据库**：服务端直连 MySQL5.7；客户端禁止直连数据库，所有数据操作走服务端接口。

## 1. 基础编码规范
1. 语言标准：默认使用 **C++17**，如需降低版本（C++11/C++14）需要显式说明；禁止无理由使用C++20及以上特性。
2. 编码风格：
    - 命名：类名大驼峰；变量/函数小驼峰；常量全大写下划线分隔；宏全部大写。
    - 缩进：4空格，禁止Tab；大括号统一使用K&R风格。
    - 内存：优先RAII，尽量使用智能指针`std::unique_ptr` / `std::shared_ptr`；裸指针必须标注所有权，禁止内存泄漏、野指针。
    - 异常：跨平台底层网络/数据库代码，**不滥用C++异常**；优先返回错误码，记录日志；Qt代码可使用Qt异常机制。
3. 头文件：头文件保护使用`#ifndef / #define / #endif`，不强制`#pragma once`（保证Linux编译器兼容）；头文件只放声明，实现放`.cpp`；尽量前置声明，减少头文件嵌套包含。
4. 类型：跨平台代码必须使用固定宽度类型 `<cstdint>`：`uint32_t`、`int64_t`等，避免`int`在Windows/Linux下长度差异。

## 2. Qt 客户端开发约束
1. Qt版本：优先Qt5.15，如需Qt6需要单独说明；信号槽优先使用新语法 `connect(sender, &Class::signal, receiver, &Class::slot)`，不推荐宏写法`SIGNAL/SLOT`。
2. 线程模型：严格遵守Qt线程规则：**QObject不能跨线程直接操作**；UI组件只能在主线程访问；网络/数据库业务放在子线程，通过信号槽回主线程更新UI。
3. 网络：**客户端直接使用原生 Winsock**。
4. 内存：QObject子类使用父对象树管理生命周期；和智能指针混用时注意双重释放。
5. 编译：Windows使用MSVC；Linux使用GCC；qMake优先。

## 3. 服务端 Socket 规则（跨平台）
1. Windows端：Winsock2，`WSAStartup(MAKEWORD(2,2), ...)`；使用`SOCKET`类型，错误码用`WSAGetLastError()`。
2. Linux端：POSIX socket，`socket()/bind()/listen()/send()/recv()`；错误码`errno`。
3. IO模型：
    - Linux服务端：默认 epoll（ET模式优先）；
    - Windows服务端：默认 IOCP；
    - 简单场景可统一使用 select 做兼容实现。
4. 字节序：所有网络数据传输必须**网络字节序（大端）**，使用`htons/htonl/ntohs/ntohl`；禁止直接send结构体（对齐问题），必须序列化。
5. 多线程：socket句柄跨线程读写必须加锁（临界区/mutex）；关闭socket要防止多线程同时调用closesocket/close。
6. 清理：Windows退出必须`WSACleanup()`；Linux退出close全部监听/连接fd。优雅关闭：先shutdown再closesocket/close。
7. 头文件：`#include <winsock2.h>`必须放在`windows.h`前面。

## 4. 多线程模型（服务端）
1. Windows原生：`CreateThread`；同步原语：`CRITICAL_SECTION`、`CreateEvent`、`CreateMutex`；句柄用完`CloseHandle`。
2. Linux原生：`pthread`；同步：`pthread_mutex`、`pthread_cond`。
3. 跨平台抽象：**网络、线程封装抽象层**，用`#ifdef _WIN32` / `#ifdef __linux__`区分平台实现；业务层代码不出现平台宏。
```cpp
#ifdef _WIN32
    // winsock / win thread code
#elif __linux__
    // posix socket / pthread code
#endif
```

1. 线程安全：共享资源必须加锁；禁止无锁读写共享变量；状态变更使用`<atomic>`原子变量。
2. 服务端线程模型：**IO 线程池 + 业务工作线程池分离**；禁止在 IO 回调里做阻塞操作（数据库、磁盘 IO）。

## 5. Linux 服务端部署与 SSH 操作规则

1. 编译器：GCC 9+；编译参数 `-Wall -Wextra -O2`，开启警告。
2. 部署通道（强制）：
   - 代码传输：**仅使用 SSH 相关工具**（scp /sftp/rsync over ssh /git over ssh）；禁止 FTP、SMB、共享文件夹、U 盘拷贝等非 SSH 方式传代码到生产服务器。
   - 远程操作：编译、部署、启动 / 停止服务、查看日志、查进程，全部通过 SSH 登录服务器执行。
   - SSH 认证：优先使用 **SSH 密钥对**（ed25519），禁止密码登录到生产服务器；私钥本地保管，不提交到代码仓库。
   - 端口：SSH 默认 22 端口，生产环境建议修改为非默认端口；禁止 root 直接登录，使用普通用户 + sudo。
3. 运行方式：默认后台守护进程；日志输出到文件；PID 文件管理进程。
4. 端口：监听端口默认非特权端口（>1024）；配置文件区分开发 / 生产环境。
5. 资源：文件描述符 ulimit 调整；内存 / 句柄泄漏用 valgrind /address sanitizer 排查。
6. 调试方式：通过 SSH 登录后使用 `gdb` / `tmux` / `journalctl` 远程调试；核心日志通过 `tail -f` 实时查看，禁止把服务器日志下载到本地分析。

## 6. MySQL5.7 C API 开发规范（服务端使用）

> 仅服务端连接数据库，客户端禁止直连。

1. 连接：`MYSQL*`连接句柄，**每个线程独立数据库连接**；禁止多线程共用同一个 MYSQL 句柄；使用连接池封装。
2. SQL 安全：**绝对禁止字符串拼接 SQL**；全部使用预处理语句 `mysql_stmt_prepare` + 绑定参数，防止 SQL 注入。
3. 错误处理：每次 API 调用后检查返回值，`mysql_error()`打印详细错误信息。
4. 字符集：连接初始化设置 `SET NAMES utf8mb4`。
5. 内存：`mysql_init`、`mysql_close`成对；结果集`MYSQL_RES*`必须`mysql_free_result()`释放。
6. 数据类型：数值、字符串、BLOB 严格绑定；时间类型统一 UTC 或东八区对齐；大字段分批读取。
7. 并发：数据库操作是 IO 阻塞，放在服务端业务工作线程，禁止阻塞 IO 线程。

## 7. 日志、错误处理与调试

1. 日志：区分等级 DEBUG / INFO / WARN / ERROR；日志带时间、线程 ID、模块名；Windows 输出文件 + 调试控制台；Linux 输出文件 + syslog 可选。
2. 错误：底层网络、数据库返回错误码，向上层传递；不要吞掉错误不处理。
3. 资源：Socket、数据库句柄、线程句柄、文件句柄，任何分支都保证释放，使用 RAII 包装。

## 8. 代码输出要求（回答强制规则）

1. 给出代码前，先说明：用途、运行角色（客户端 / 服务端）、平台、依赖、编译方式。
2. 代码必须可编译、可运行，标记平台差异部分；标注头文件。
3. 遇到 bug：优先分析调用栈、错误码（WSAGetLastError /errno/mysql error），分步定位。
4. 代码评审：检查内存安全、线程安全、跨平台兼容性、SQL 注入风险、socket 资源泄漏、客户端是否违规直连数据库。
5. 重构：优先保证接口不变，逐步替换实现；不一次性大规模重写。
6. 提问时优先给出最小可复现 Demo，再扩展完整工程。

## 9. 禁止行为清单

- ❌ 客户端直连 MySQL 数据库
- ❌ 服务端业务层出现平台宏（`#ifdef _WIN32`）不做抽象封装
- ❌ 直接发送内存结构体二进制到 socket（不序列化）
- ❌ 多线程共用同一个 MySQL 连接句柄
- ❌ 在 Qt 主线程执行阻塞的数据库 / 长耗时网络 IO
- ❌ 裸 socket 多线程无锁并发读写
- ❌ 拼接字符串组装 SQL 语句
- ❌ 忘记关闭 socket、数据库句柄、线程句柄造成资源泄漏
- ❌ 跨线程直接修改 QObject 成员变量
