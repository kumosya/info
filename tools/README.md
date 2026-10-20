# Linux 宿主工具

- `inpkg`：Linux x86-64 ELF 二进制，负责包校验、元数据、摘要、包集与 sysroot 安装。
- `infoide`：Linux x86-64 ELF 二进制，生成 `infoide-project.json`，按项目参数编译、链接并调用同目录 inpkg 打包。
- `osinfo-host`：本仓库 `host/` 的输出，处理配置生成、布局、SDK、镜像和审计。configure 将其构建到本目录。
- `unzip`：ZIP 复合包解压二进制；来源为 Debian unzip 6.0-29+deb13u1 amd64，许可证为 [unzip-copyright](licenses/unzip-copyright)。依赖宿主 libc 和 libbz2。

inpkg 和 infoide 的源码在各自独立项目维护，本仓库只携带可执行文件；更新工具后重新 configure。常规 OS 构建不编译、不执行相邻项目。host 自身仅调用本目录 inpkg 的 inspect/inspect-manifest/digest/catalog 接口，不链接相邻项目源码。

正常入口是根目录 `./configure`，随后 `make`。ZIP 先验证目录成员、类型和边界，再展开到包目录；不接受路径穿越、链接成员及多卷/ZIP64 容器。
