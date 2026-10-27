# 进程凭据与认证

实现位于 `src/kernel/task/credentials/credentials.cc`，状态由 `include/kernel/task.h` 中的 `kernel::task::Task` 保存。用户态请求通过 task.kernel 分派，具体入口见 [任务机制服务](service.md)。

## 保存的数据

每个 Task 保存真实、有效和保存的 UID/GID，以及附加组列表。真实身份用于归属判断，有效身份用于多数权限检查，保存身份支持受限的身份切换。凭据变更在 Task 锁内完成。

Task 另存认证会话编号、认证时的 UID/GID，以及内核授予的能力位。认证会话和进程会话 SID 有关联，但认证信息不会代替每次操作的权限检查。

读取接口涉及 uid、gid、effectiveUid、effectiveGid；`credentials(info)` 在同一锁内复制完整凭据和附加组，供 IPC 序列化使用。

## 修改凭据

`changeCredentials(operation, first, second, third, groups)` 实现当前进程的 POSIX 凭据变更，成功返回 0，失败返回正的 errno 值。operation 是服务内部操作编号；应用接口由各自的成员函数封装。

| operation | 对应操作 | 参数数据 |
| --- | --- | --- |
| 1 / 2 | setuid / setgid | first 为目标身份。 |
| 3 / 4 | setreuid / setregid | first 为真实身份，second 为有效身份。 |
| 5 / 6 | setresuid / setresgid | first、second、third 分别为真实、有效、保存身份。 |
| 7 / 8 | seteuid / setegid | first 为有效身份。 |
| 9 | setgroups | first 为数量，groups 指向附加组编号。 |

多身份设置中的 UINT32_MAX 表示该项保持不变；单身份设置不接受它。有效 UID 为 0 时可以设置任意合法身份，普通进程只能按相应操作的规则在已有真实、有效和保存身份间切换。附加组修改要求有效 UID 为 0，并限制为 `os::maximumSupplementaryGroups` 个。

真实身份变化会清除现有认证会话标识。身份或附加组变化会使当前进程不可转储，避免凭据变化后继续按旧条件输出敏感内存。

`setCredentials(uid, gid, effectiveOnly)` 是经过服务入口授权后的内核设置函数。effectiveOnly 为 true 时只改有效 UID/GID，否则同时设置真实和保存身份；该成员函数本身不校验调用者权限，权限检查在 task.kernel 的 SetCredentials 分支完成。

## 建立和读取认证会话

| 成员函数 | 功能与参数 |
| --- | --- |
| `authentication(session, uid, gid)` | 在锁内取得认证会话编号及认证身份，三个参数接收结果。 |
| `authenticate(uid, gid, preserveCredentials, groupCount, groups)` | 为目标进程建立认证记录。preserveCredentials 为 false 时更新真实、有效、保存身份和附加组；为 true 时保留凭据并要求现有效身份匹配。 |

authenticate 拒绝已终止进程、受监督服务、越界身份和无效附加组。成功后认证会话编号取当前 sessionId。谁可以替其他进程认证，由 AuthenticateSession 服务入口限制为持 Users 能力的 Users 角色。

普通进程只能查询自身认证信息。跨进程查询允许持 Users 能力的 Users 服务，以及持 ReadIdentity 能力的受监督身份观察服务。

## 能力与继承

`grantCapabilities(capabilities)` 将指定能力位加入进程；`hasCapability(capability)` 判断全部指定能力位是否存在。当前能力包括 Device、Spawn、Display、Users、Pci、Memory 和 ReadIdentity，分别控制硬件资源、启动映像、显示映射、身份管理、PCI、内存机制及认证信息读取。

能力由加载和受控服务启动路径授予，普通进程不能靠修改 IPC 中的字段自行增加。能力位、服务角色、句柄权限和 UID 检查分别解决不同问题，具体服务可要求同时满足其中多项。

创建 Task 时继承父进程凭据、附加组和认证信息；fork 再复制相关状态并保持句柄继承规则。exec 保留身份，将保存 UID/GID 更新为有效身份，并按新映像提交逻辑重算可转储条件。认证会话、凭据变化和 core 输出之间的关系见 [信号与 core](core.md)。
