# 数据、状态与 XDG 目录

`#include <os/directories.h>` 提供 `os::Directories` 类。查询返回调用者缓冲区；输入无效、空间不足或用户运行目录不安全时返回 `nullptr`。查询不会创建目录，不使用宏或新增 syscall。

| 方法 | 系统默认目录 | 用户默认目录 |
|---|---|---|
| `data` | `/usr/share` | `$HOME/.local/share` |
| `state` | `/var/lib` | `$HOME/.local/state` |
| `config` | `/etc/xdg` | `$HOME/.config` |
| `cache` | `/var/cache` | `$HOME/.cache` |
| `runtime` | `/run` | `/run/user/<uid>` |
| `log` | `/var/log` | `$XDG_STATE_HOME/<应用>/log` |

```cpp
char path[512];
if (os::Directories::data(os::DirectoryScope::User, path, sizeof(path), "editor")) {
    // 默认 /home/user/.local/share/editor；调用者按需创建应用子目录。
}
```

用户查询实时读取 `XDG_DATA_HOME`、`XDG_STATE_HOME`、`XDG_CONFIG_HOME`、`XDG_CACHE_HOME` 和 `XDG_RUNTIME_DIR`。空值和相对路径使用默认值。`dataSearch(index, ...)`、`configSearch(index, ...)` 的索引 0 为用户路径，之后依次枚举 `XDG_DATA_DIRS`、`XDG_CONFIG_DIRS` 中有效的绝对路径；默认分别为 `/usr/local/share:/usr/share`、`/etc/xdg`。列表中的空项及相对项被忽略。应用名只允许单个目录分量，拒绝 `.`、`..`、斜杠和控制字符。

运行目录必须已存在、由当前有效 UID 所有且权限严格为 0700。users 服务负责首次会话建立与最后会话退出时的创建、清理，并在启动时清理受管的 `/run/user`。持久状态应写入 `state`，不要放在运行目录。

登录会清除上一个用户的环境，设置 HOME、USER、LOGNAME、SHELL、PATH 与全部七个 XDG 变量。同身份普通交互 shell 保留有效自定义路径；`setenv`、`unsetenv`、`clearenv` 的变化会立即反映到 OS 库查询。库没有写入宿主 Windows/WSL 环境变量。

镜像布局增加 `/media`、`/proc`、`/run/lock`、`/lib`、`/var/{lib,cache,log,tmp}`；`/tmp` 和 `/var/tmp` 使用 01777。`/proc` 当前只提供挂载目录，尚无 procfs。用户旧 `app` 合并迁移到 `.local`，路径冲突保留源文件并记录，避免覆盖已有数据。用户包数据库位于 `.local/state/inpkg`。

布局配置集中在 `config/layout.conf`，由 C++ 工具 `build/host/osinfo-host layout` 供镜像制作与隔离测试共用。WSL 普通用户制作的 ext3 镜像通过离线 debugfs 设置真实 UID/GID 与权限，避免将宿主开发者 UID 错写成系统文件所有者。
