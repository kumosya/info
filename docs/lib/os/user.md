# os/user.h


## 内容与边界

User 取代用户客户端，保存 users 服务连接，分别提供 lookup、lookupName、login、graphicalLogin、authorizeSudo、changePassword、querySession、beginSession、autoLogin、list，不公开操作编号和万能 request。

User::Profile 嵌套在 User 内，直接保存只读账户资料，提供 getUid、getGid、getName、getHome、getShell。响应完整验证后统一更新，失败保留旧结果。

临时口令副本使用不可被优化掉的清除方法，不长期缓存或记录密码。口令哈希、shadow 文件和会话目录归 users 服务。服务没有增加账户能力时，不公开无法兑现的函数。

## 枚举

本头文件不公开枚举。功能操作使用各自的成员函数，不通过公开操作编号切换。

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

当前公共头文件没有声明需要公开的独立数值或字符串常量。

## 全局变量

当前公共头文件没有导出可写全局变量。账户结果属于 Profile，认证密码只作短期输入，不作为可写全局变量。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### User
这个类表示users 服务的账户查询和认证连接。

#### 构造与析构

```cpp
User();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


#### 操作

```cpp
int login(const char* name, const char* password, User::Profile& profile);
```

用途：验证用户名和密码，并请求 users 服务建立当前终端登录身份。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `const char* password`：本次认证使用的密码，函数返回前不应修改，库不长期保存。
- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int authorizeSudo(const char* password, User::Profile& profile);
```

用途：根据真实调用者、密码和 sudoers 请求提权。

参数：

- `const char* password`：本次认证使用的密码，函数返回前不应修改，库不长期保存。
- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int querySession(uint32_t taskId, User::Profile& profile, uint64_t& sessionTask);
```

用途：读取给定登录任务的账户和服务确认的会话任务编号。

参数：

- `uint32_t taskId`：目标进程编号。
- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。
- `uint64_t& sessionTask`：输出服务确认的登录任务编号。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int beginSession(uint32_t uid, User::Profile& profile);
```

用途：为已认证账户准备会话目录等资源，不代替身份认证。

参数：

- `uint32_t uid`：账户或目标用户编号。
- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int autoLogin(User::Profile& profile);
```

用途：请求服务按系统自动登录策略认证，不跳过服务端限制。

参数：

- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int list(uint32_t index, User::Profile& profile);
```

用途：按序号读取一个允许登录的账户，供登录窗口显示账户列表。

参数：

- `uint32_t index`：从 0 开始的条目或参数序号。
- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int graphicalLogin(const char* name, const char* password, User::Profile& profile);
```

用途：验证图形登录，服务同时检查调用者是否是允许认证的登录宿主。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `const char* password`：本次认证使用的密码，函数返回前不应修改，库不长期保存。
- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int lookup(uint32_t uid, User::Profile& profile);
```

用途：按 uid 读取账户资料，成功时填写 profile；不验证密码，也不改变调用者身份。

参数：

- `uint32_t uid`：账户或目标用户编号。
- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int lookupName(const char* name, User::Profile& profile);
```

用途：按用户名读取账户资料，不进行登录认证。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `User::Profile& profile`：成功时接收账户资料，失败保持原内容。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int changePassword(const char* name, const char* oldPassword, const char* newPassword);
```

用途：验证权限和旧密码后，为指定账户设置新密码。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `const char* oldPassword`：当前密码，用于需要验证旧密码的改密请求。
- `const char* newPassword`：希望设置的新密码。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open();
```

用途：连接 users 服务，发现失败或服务未就绪时返回错误。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：释放保存的认证服务连接；不改变已经建立的进程身份。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。

### User::Profile

这个类表示经过验证的用户名、用户编号、主目录和登录 shell。

#### getter

```cpp
uint32_t getUid() const;
```

数据：真实用户编号。

```cpp
uint32_t getGid() const;
```

数据：真实用户组编号。

```cpp
const char* getName() const;
```

数据：名称。

```cpp
const char* getHome() const;
```

数据：账户主目录。

```cpp
const char* getShell() const;
```

数据：账户登录 shell 路径。

## 实现文件

`lib/os/user.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
