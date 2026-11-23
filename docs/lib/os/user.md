# 用户账户

## 对象

### os::User

用户服务连接对象，提供账户、认证和登录会话访问。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `User()` | 构造User对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int open()` | 打开对象连接 | — |
| `int close()` | 关闭对象连接 | — |
| `int lookup(uint32_t uid, Profile& profile)` | 按用户编号读取账户资料 | `uid`：用户编号<br>`profile`：接收账户资料的对象 |
| `int lookupName(const char* name, Profile& profile)` | 按用户名读取账户资料 | `name`：对象名称<br>`profile`：接收账户资料的对象 |
| `int login(const char* name, const char* password, Profile& profile)` | 验证用户名和密码 | `name`：对象名称<br>`password`：认证密码<br>`profile`：接收账户资料的对象 |
| `int authorizeSudo(const char* password, Profile& profile)` | 根据真实调用者、密码和sudoers请求提权 | `password`：认证密码<br>`profile`：接收账户资料的对象 |
| `int changePassword(const char* name, const char* password, const char* replacement)` | 修改账户密码 | `name`：对象名称<br>`password`：认证密码<br>`replacement`：新密码 |
| `int querySession(uint32_t taskId, Profile& profile, uint64_t& sessionTask)` | 读取给定登录任务的账户和服务确认的会话任务编号 | `taskId`：进程编号<br>`profile`：接收账户资料的对象<br>`sessionTask`：接收登录会话进程编号 |
| `int beginSession(uint32_t uid, Profile& profile)` | 为已认证账户准备会话目录等资源 | `uid`：用户编号<br>`profile`：接收账户资料的对象 |
| `int autoLogin(Profile& profile)` | 请求服务按系统自动登录策略认证 | `profile`：接收账户资料的对象 |
| `int list(uint32_t index, Profile& profile)` | 按序号读取账户资料 | `index`：项目索引<br>`profile`：接收账户资料的对象 |
| `int graphicalLogin(const char* name, const char* password, Profile& profile)` | 验证图形登录 | `name`：对象名称<br>`password`：认证密码<br>`profile`：接收账户资料的对象 |

### os::User::Profile

用户资料对象，保存用户名、身份、主目录和登录程序。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint32_t getUid() const` | 读取真实用户编号 | — |
| `uint32_t getGid() const` | 读取真实用户组编号 | — |
| `const char* getName() const` | 读取名称 | — |
| `const char* getHome() const` | 读取账户主目录 | — |
| `const char* getShell() const` | 读取账户登录shell路径 | — |

### os::SessionIdentity

任务认证身份连接对象，保存目标任务、任务服务通道和身份查询结果。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit SessionIdentity(uint64_t task)` | 构造SessionIdentity对象 | `task`：目标进程 |
| `~SessionIdentity() = default` | 析构SessionIdentity对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int query(SessionIdentityInfo& identity)` | 查询目标任务的认证身份 | `identity`：内存对象标识 |
| `int establish(uint32_t uid, uint32_t gid, bool preserveCredentials = false, uint32_t groupCount = 0, const uint32_t* groups = nullptr)` | 登记登录会话身份 | `uid`：用户编号<br>`gid`：用户组编号<br>`preserveCredentials`：保留进程当前身份<br>`groupCount`：附加用户组数量<br>`groups`：附加用户组编号数组 |

### os::SessionCleanup

认证会话清理对象，保存待结束的认证会话编号。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit SessionCleanup(uint64_t session)` | 构造SessionCleanup对象 | `session`：认证会话编号 |
| `~SessionCleanup() = default` | 析构SessionCleanup对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void terminate()` | 结束认证会话所属的用户进程 | — |
