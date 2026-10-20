> 后续要求已更新：仅使用 packages/，base-tools 拆分四包，删除全部 Python（含格式化例外），InfoIDE 改用纯光盘调试和独立 Linux 安装包。当前实现与验证见 [补充记录](cd-only-infoide-progress.md)。以下保留当时记录。

# 本地二进制工具、configure 与 InfoIDE 更新

本记录承接 [上一轮实施记录](inpkg-linux-python-removal-progress.md)，更新日期为 2026-10-20。旧记录中的相邻项目命令、config/packages.conf、smoke.conf 和 osinfo 命令名已由本轮流程替代；架构文件保持原样。

## 当前构建流程

```sh
./configure
make -j
make check sdk
```

1. `tools/inpkg`、`tools/infoide` 是 Linux ELF 二进制，不复制这两个项目的工具源码。最初添加的源码副本已撤掉。
2. `./configure` 检查 Linux 环境、编译器 32/64 位及 C++20 能力、工具可执行性，并构建本仓库的 `tools/osinfo-host`。host 不再链接或读取相邻项目源文件，包解析与摘要通过本目录 inpkg 的元数据接口完成。
3. 应用与外部服务先放入 `package/`。目录不存在时，按 configure 开头可修改、可追加的 RELEASE_URLS 下载 GitHub Release 资产；已有目录走离线流程，空目录、缺包和损坏包不会隐式下载掩盖错误。
4. ZIP 复合包根据后缀识别，验证路径和成员类型后直接展开到包目录，随后递归校验每个 .inpk。拒绝路径穿越、符号链接、重复身份、安装路径冲突及缺失的引导/会话程序。
5. configure 统一生成编译器与目录配置、包集、Make 清单、GRUB、内核 boot 表和 init 会话/启动数据；最后才发布成功配置。失败配置不能继续 make，make 不自动运行 configure 或环境探测。
6. 根 Makefile 只构建内核、系统服务、运行库和系统解释器；edit、inpkg、guiw、基础工具等作为发行包输入。edit 的独立项目配置位于 user/edit/infoide-project.json，常规 OS 构建不编译它。
7. 移除 config/smoke.conf；测试程序选择与结果断言留在测试脚本中。手写 config/packages.conf 也由扫描生成的 build/packages.conf 替代。

普通包更新仍使用 upgrade --install-missing；系统解释器和版本文件仍由系统构建直接部署。clean 保留 package 输入目录。

## InfoIDE 与标准头文件

命令、帮助、错误信息、可执行文件和生成代码均使用 infoide。create 生成 infoide-project.json、使用 cstdio/std::puts 的 C++ 模板，以及完整、可编辑的编译参数。

cppflags 对所有源文件生效，cflags/cxxflags/asflags 分别用于 C/C++/.S；ldflags/ldlibs 决定附加链接参数。字段为参数数组，先全局、后目标；不隐式添加标准、优化、诊断或 freestanding 等策略参数。SDK 头目录、目标类型、CRT 和链接脚本仍按所选 SDK 解析。

inpkg、guiw、osinfo-base-tools 的独立项目配置与调用入口已迁移到 infoide-project.json/infoide；基础工具自行携带压缩实现，不再依赖 OS SDK 提供应用私有头文件。

主仓库及关联开发项目共 58 个正式 C++ 源文件完成标准 C 头引用替换，如 stdio.h → cstdio、stdlib.h → cstdlib、string.h → cstring、stdint.h → cstdint。补充 SDK 的 csignal。C 文件、POSIX 头与 cxxx 包装头内部的 C 接口保留各自用途。

## 验证记录

| 验证 | 日志 |
| --- | --- |
| 常规 configure、编译、SDK 与审计 | build/configure-main.log、build/configure-build.log |
| InfoIDE 模板、全局/目标参数、C/C++/.S 分流、链接映射、失败处理 | build/configure-cli-final-tests.log |
| 离线包扫描、ZIP 直接展开、七包部署、下载流程、失败配置隔离、ZIP 路径穿越拒绝 | build/configure-final-tests.log |
| 独立输出目录全量 all/check/sdk | build/configure-clean-setup.log、configure-clean-build.log |
| 默认入口的最终 all/check/sdk 与目标测试程序编译 | build/configure-final-build.log；目标测试编译也已验证 |
| 包事务与中断恢复回归 | build/configure-inpkg-tests.log |
| 独立 edit/inpkg/guiw/基础工具使用新配置重新编译和打包 | build/configure-edit.log、configure-inpkg-target.log、configure-guiw.log、configure-base-tools.log |
| 标准头迁移清单 | build/cxx-header-migration.log |

下载流程测试使用受控 GitHub URL 和本地下载器替身验证分支、包内容与失败处理；未声称真实远端 Release 已下载成功。默认 URL 根据现有仓库地址列出，匿名 GitHub API 检查返回 404，可能需要改为实际可访问的资产地址或手动准备发行包。当前工作区使用已准备的本地包。

ZIP 解压二进制在 tools/unzip，来源与许可见 tools/README.md 和 tools/licenses/unzip-copyright。未全局安装软件。未执行 Git 提交、推送或分支操作。
