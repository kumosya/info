> 后续要求已更新：仅使用 packages/，base-tools 拆分四包，删除全部 Python（含格式化例外），InfoIDE 改用纯光盘调试和独立 Linux 安装包。当前实现与验证见 [补充记录](cd-only-infoide-progress.md)。以下保留当时记录。

# 双平台 inpkg、集中配置与去 Python 执行 Prompt

日期：2026-10-20。按 [最新大纲](inpkg-linux-python-removal-outline.md) 实施，本文与大纲构成完整执行规格。用户已授权生成本文后直接开发。

## 目标和约束

1. 首先将 `src/kernel/system/service/service.cc` 移到 `src/kernel/task/sys/service.cc`，保留 system.kernel 名称、权限和行为。旧目录不保留另一份实现，构建和当前文档指向新路径。
2. `D:/osinfo/inpkg` 提供原生 Linux 和 OSInfo 两个 C++ 程序，共享包核心。Linux 安装/升级/卸载/已安装查询必须显式使用 --sysroot，拒绝宿主根及逃逸路径。
3. install 只首次安装；upgrade 只更新已安装包；upgrade --install-missing 在确实未安装时首次安装。损坏数据库、锁、校验或更新失败不能触发首次安装。保留其他包和用户修改的文件，准备/备份/提交有状态记录，失败可恢复。
4. pack/check/import、主仓库包元数据与批量预检均使用 C++ inpkg；osinfo pkg 构建后调用它。Linux inpkg 可以不依赖 SDK 自举，目标 inpkg 不能成为其自身打包前提。
5. 服务、目录、应用和包集分别由 config/services.conf、layout.conf、applications.conf、packages.conf 唯一维护。C++ 工具校验并生成 Make/GRUB/内核引导/init/测试需要的产物；shell 只编排，不 source/eval 数据配置。
6. 迁移全部 Python 工具、SDK 副本、审计和测试为 C++/shell，唯一例外是根目录 clang-format.py。保留原测试能力，不能以删除测试实现去 Python。
7. 除正式源码外也核对构建目录、SDK 与包负载，不再生成 Python 副本。历史辅助脚本有用则迁移，无用则按清单清理。保留已有用户改动；不改架构文件、不提交、不创建分支、不做无关全局安装或环境设置。

## 分阶段实施

1. 记录当前工作区、规则和脚本消费者；完成 system.kernel 归位。
2. 实现共享 manifest、归档、数据库、事务和平台适配，先验证 Linux install/upgrade/install-missing/remove 及失败恢复。
3. 建立集中配置和生成器，接通 OS 构建、boot 模块及服务身份/启动依赖，保持编号稳定。
4. 替换 SDK/布局/审计/包处理工具，切换 C++ CLI 与各项目构建。
5. 迁移 shell/C++ 镜像和桌面输入测试，清理已替代的 Python 文件、生成副本及缓存。
6. 全新目录构建，重建并导入所有关联包，运行包事务、边界、宿主内存/IPC、单核/多核 MM、任务生命周期、桌面动态链接与输入回归。
7. 更新非架构文档、使用命令与实施记录，说明真实事务保证、恢复方式及未验证限制。

## 具体检查

- 路径校验覆盖包归档成员、安装映射、数据库、临时文件、备份及 sysroot 的父目录；禁止链接和特殊成员逃逸。
- 解析与安装完整验证 gzip/ustar、重复条目、负载清单、文件归属、权限、冲突和系统保护路径；保持旧包及数据库可读取。
- 同名包可增/删文件和同版本重装；整批先预检，逐包事务，不能宣称多文件全局原子提交。中断恢复要有明确状态和测试。
- 服务源文件按目标精确选择；配置排序不能改变 ProgramId、ServiceRole 或 syscall ABI。运行时普通包不能授予内核能力。
- SDK 头文件摘要、启动对象与解释器部署、layout 权限和 XDG、审计边界及 QMP 真实输入能力不得丢失。
- 最终全量扫描只允许 clang-format.py；构建、安装、测试的入口不依赖 Python。架构文件差异必须为空。

使用中文持续报告进展，遇到错误修复后继续，不把仅有 Prompt、未经验证的替代工具或阶段性成果当作全部完成。
