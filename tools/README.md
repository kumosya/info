# Linux 宿主工具

- `osinfo-host`：本仓库 `host/` 的输出，处理配置生成、布局、SDK、镜像和审计。configure 将其构建到本目录。
- `unzip`：ZIP 复合包解压二进制；来源为 Debian unzip 6.0-29+deb13u1 amd64，许可证为 [unzip-copyright](licenses/unzip-copyright)。依赖宿主 libc 和 libbz2。

host 自身仅调用本目录 inpkg 的 inspect/inspect-manifest/digest/catalog 接口，不链接相邻项目源码。

正常入口是根目录 `./configure`，随后 `make`。ZIP 先验证目录成员、类型和边界，再展开到包目录；不接受路径穿越、链接成员及多卷/ZIP64 容器。
