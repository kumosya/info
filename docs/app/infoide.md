# 宿主 SDK 与 Info IDE

Info IDE 运行在 Windows/WSL，源码位于 `D:/osinfo/infoide`。OSInfo 当前没有本机编译工具链，因此应用在宿主交叉编译、打包，再安装到目标系统。

```powershell
# 先在 OS 仓库的 WSL 环境执行 make sdk
python D:/osinfo/infoide/infoide.py --project D:/osinfo/guiw
python D:/osinfo/infoide/infoide.py --project D:/osinfo/inpkg
python D:/osinfo/infoide/infoide.py --create D:/osinfo/hello
```

IDE 提供项目文件列表、文本编辑、新建与保存、构建日志、编译与 `.inpk` 打包。Ctrl+S 保存，F5 构建。项目外路径和跨源写入被拒绝；保存采用内容版本检查，外部改动不会被静默覆盖。它只监听本机回环地址并使用每次启动生成的令牌。当前没有语言服务器、补全和断点调试。

## 独立项目

`make sdk` 导出 `build/sdk`，包含公共头文件、共享库、CRT 入口、ELF 链接脚本和构建/打包工具。SDK 不复制内核私有头文件，`sdk.json` 记录文件摘要。独立项目只需要此 SDK，构建输出留在自己的 `build/`。

```json
{
  "name": "hello",
  "version": "0.1",
  "targets": [{
    "name": "hello",
    "sources": ["src/*.cc"],
    "libraries": ["c", "os-c++"]
  }],
  "install": {"hello": "/usr/bin/hello"}
}
```

`targets[].libraries` 选择库，生成对应 `DT_NEEDED`；可选 SDK 库有 `c`、`c++`、`os-c++`、`osgui-c++`。也可先声明 `shared` 目标（例如 libsample.so），后续目标使用 `libraries: ["sample", "c", "os-c++"]` 链接项目自己的库；`runpath` 可设置绝对搜索路径或 `$ORIGIN`。共享库与应用在 install 中分别指定安装位置。应用默认使用 `/lib/ld-osinfo-x86-64.so`。`boot-service` 目标供需要由引导模块启动的服务使用，仍执行 Ring3 检查。

```sh
python3 /path/sdk/tools/build-project.py --project /path/hello --sdk /path/sdk
python3 script/import-package.py /path/hello/build/packages/hello.inpk
```

OS 仓库的 `script/external-packages.json` 声明必需的 guiw、inpkg 包。缺包时镜像构建直接报错，不会访问或编译相邻应用源码。`script/import-package.py` 校验包格式、负载清单及标识后原子导入；安装器统一检查目标冲突。guiw 服务的 `/boot/guiw.elf` 从安装后的包负载复制，避免使用旧 build 缓存。

构建工具使用参数数组调用编译器，目标文件、可执行文件和归档经临时文件替换，失败构建不会把半成品作为下一次可安装产物。包缓存校验实际字节，不单独依赖 mtime。
