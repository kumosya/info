> 后续要求已更新：仅使用 packages/，base-tools 拆分四包，删除全部 Python（含格式化例外），InfoIDE 改用纯光盘调试和独立 Linux 安装包。当前实现与验证见 [补充记录](cd-only-infoide-progress.md)。以下保留当时记录。

# Linux/OSInfo 双平台 inpkg 与 Python 全面替换实施大纲

更新日期：2026-10-20。本大纲承接 [v0.1.1 实施记录](v0.1.1-refinement-progress.md)，记录用户最新补充要求。已授权实施，执行规格见 [实施 Prompt](inpkg-linux-python-removal-prompt.md)。

## 1. 已确定的要求与范围

1. 在 `D:/osinfo/inpkg` 同时维护 Linux 原生 C++ 版和 OSInfo C++ 版，公共包处理实现共享，平台入口与适配独立。
2. Linux 版操作已安装包时必须显式指定 `--sysroot PATH`，PATH 是 OSInfo 已挂载根目录或离线 staging 根目录。
3. 使用显式 `upgrade` 更新已安装包。增加 `--install-missing`，仅在目标范围内没有安装该包时执行首次安装。
4. 项目内部只使用 shell 或 C++ 完成工具与脚本逻辑。唯一允许保留的 Python 文件是 OS 仓库根目录的 `D:/osinfo/info/clang-format.py`，即用户所指格式化脚本；不擅自将例外扩大到其他格式化文件。
5. 替换范围包括 OS 仓库、inpkg、infoide、osinfo-base-tools、guiw 中的构建、SDK、镜像布局、审计、测试及包处理脚本，也包括生成目录中的 Python 副本。不能只删除后缀或用 shell 包装 Python。
6. 服务、目录布局、应用程序及包集分别拥有集中配置文件；shell、Makefile、GRUB、内核引导表、init 和测试流程使用配置或其生成产物，不能各自维护相同清单。
7. 继续保持 Ring3/Ring0 边界、系统文件与普通包边界；架构文件保持原样，不主动执行 Git 提交、分支或推送操作。
8. 将 `system.kernel` 的实现从 `src/kernel/system/service/` 迁入 `src/kernel/task/sys/`，更新构建、配置及当前文档；端点名称和接口保持不变。

这些要求取代上一阶段“保留现有 Python 脚本/SDK 兼容入口”的范围限制。历史实施记录保持历史语境，不能据此继续保留当前要求禁止的工具。

## 2. upgrade 与 install 的精确语义

```sh
inpkg install hello.inpk --sysroot /mnt/osinfo
inpkg upgrade hello.inpk --sysroot /mnt/osinfo
inpkg upgrade hello.inpk --sysroot /mnt/osinfo --install-missing
```

| 命令 | 包未安装 | 同名包已安装 |
| --- | --- | --- |
| install | 首次安装 | 报告已安装，要求使用 upgrade |
| upgrade | 明确失败 | 执行更新 |
| upgrade --install-missing | 执行首次安装 | 执行更新 |

- `--install-missing` 仅处理“包未安装”，不能将归档损坏、数据库损坏、锁冲突、路径冲突或升级失败当作未安装后重新安装。
- 同名包升级允许更新、增加和删除该包拥有的文件；拒绝覆盖其他包拥有的路径及无归属的现存文件。
- 对用户修改过的文件保留或明确报告冲突，不静默覆盖；不能通过升级选项顺带放开系统保护路径。
- 同版本允许显式重装，用于开发镜像同步；以用户提供的同名归档作为更新来源，不使用字符串字典序判断版本新旧，不实现自动选版本或依赖求解。
- 更新先完整校验，再准备新文件和旧文件备份，失败恢复旧负载与数据库。新数据库记录必须在文件部署完成后提交。
- 多文件安装不能宣称一次 rename 就实现整包原子性。记录事务状态，明确处理中断后的恢复，清理临时文件时不得误删旧文件或用户文件。
- 批量包操作先对整个输入集合检查归档、名字、归属和目标路径，再按确定顺序安装/升级。整批预检不等于整批崩溃原子提交，失败边界必须说明。
- OSInfo 版本继续支持 system/local/root 用户范围，使用相同 upgrade 语义；原有 `--root` 的用户范围含义不与 Linux `--sysroot` 混用。

## 3. 双平台项目组织与命令

建议按最小职责组织：

```text
inpkg/
  src/
    common/          # manifest、归档校验、清单、安装/升级/卸载规则
    linux/           # Linux CLI、sysroot、文件/权限/锁适配
    osinfo/          # OSInfo CLI、账户/XDG/服务接口适配
  tests/
  Makefile
  osinfo-project.json
  build/linux/inpkg
  build/packages/inpkg/inpkg
```

共享层不得依赖 OSInfo 私有头文件或 Linux 专属文件接口；只把实际平台差异放入适配层，不复制两套安装规则。普通包不执行安装钩子，不自动挂载、格式化或运行 sysroot 内的程序。

拟定用户接口：

```sh
inpkg pack PAYLOAD --manifest package.conf --output hello.inpk
inpkg check hello.inpk
inpkg import hello.inpk --package-dir /path/to/info/packages/external
inpkg install hello.inpk --sysroot /mnt/osinfo
inpkg upgrade hello.inpk --sysroot /mnt/osinfo --install-missing
inpkg list --sysroot /mnt/osinfo
inpkg info hello --sysroot /mnt/osinfo
inpkg remove hello --sysroot /mnt/osinfo
```

pack/check/import 操作开发文件，不要求 sysroot；Linux 的安装、升级、卸载、已安装列表及数据库查询要求 sysroot。包内 `/usr/bin/hello` 映射到 `PATH/usr/bin/hello`，数据库使用 `PATH/var/lib/inpkg`。拒绝宿主 `/`、不存在或不合法的根目录、路径穿越和符号链接逃逸。

保持现有 ustar tar.gz `.inpk`、package.conf 和旧包数据库可读取；需要新增事务或文件摘要记录时，与旧记录兼容并明确迁移方式。目录、文件权限及目标 UID/GID 按 OSInfo 布局处理，不继承开发者宿主身份。

## 4. 集中配置：每类信息只维护一处

配置集中在 OS 仓库 `config/`，初步划分如下。具体字段在实现前确定，禁止在消费者中复制维护另一套同义列表。

| 文件 | 唯一维护的信息 | 主要消费者 |
| --- | --- | --- |
| `config/services.conf` | 服务名称、Ring 级别、源码目录/目标、固定 boot 身份、模块名、部署路径、启动依赖及就绪条件 | 构建、GRUB 生成、内核引导目录、init、镜像复制、smoke |
| `config/layout.conf` | 系统/用户目录、模式、所有者策略、必要系统文件的来源和目标 | 布局初始化、install、离线镜像权限归一化、测试镜像 |
| `config/applications.conf` | OS 仓库内应用目标、源码入口、所属包，以及普通会话程序的路径/参数 | 应用构建与初始会话启动配置 |
| `config/packages.conf` | 系统包配置入口、必需外部包名称/来源、导入位置与镜像包集 | 包扫描、Make 清单生成、导入、批量检查与安装 |

- 独立应用项目继续用各自的 `osinfo-project.json` 维护源码、编译器、依赖库及安装映射；系统配置引用项目/包身份，不复制其逐文件安装映射。
- 现有系统包 `lib/*.conf` 仍可作为各包自己的 manifest 来源，集中包集配置登记其位置；逐文件映射不再抄到应用清单里。
- 配置是声明式数据，不是能任意执行的 shell 源码。禁止 eval、source 非代码配置或把配置内容拼成 shell 命令。
- 采用有固定字段、明确引用和可校验转义规则的简单文本格式；C++ 工具负责需要结构化处理的校验与生成，shell 负责串联步骤，避免各 shell 独立解析复杂结构。
- C++ 配置生成工具放在 OS 仓库宿主工具目录，例如 `tools/system-config/`；不把服务启动生成器硬塞进包管理器。
- Make 变量、GRUB 模块列表、内核服务表和 init 启动表生成到 `build/generated/` 等输出目录。生成产物不作为人工修改入口，配置变化能自动触发重新生成。
- ABI 中既有 ProgramId、ServiceRole 和 syscall 编号保持稳定；服务重排不能通过数组位置隐式改变身份编号。生成阶段检查重名、缺项、依赖环、缺失源码及错误的 Ring 归属。
- 配置中的服务角色/能力是受校验的构建输入，不允许运行时包或磁盘程序自行申请 Ring0、固定 boot 角色和内核能力。init 中真正的监管、重连与故障处理逻辑保留在代码中。
- `/etc/fstab`、`/etc/hostname`、版本文件等各自用途的系统配置继续存在；集中配置负责部署它们，不把文件内容再复制到多个脚本。

## 5. Python 替换清单

初步调查：正式源码中有格式化脚本及 11 个待替换 Python 文件；计入被忽略的构建目录后，关联项目共发现 61 个 Python 文件。数量只用于当前调查，最终重新全量扫描。

| 现有实现 | 替代方向 |
| --- | --- |
| `script/packages.py` | C++ inpkg 的包扫描、Make 清单输出、pack/check、批量 install/upgrade |
| `script/import-package.py` | C++ inpkg import |
| `script/build-project.py` | 现有 C++ osinfo build/pkg；删除旧 SDK 兼容入口 |
| `script/export-sdk.py` | C++ SDK 导出工具，保留头文件/库/CRT/摘要和 sdk.json 生成 |
| `script/layout.py` | C++ 布局/权限工具读取 layout.conf，shell 编排挂载与外部系统工具 |
| `script/check-service-imports.py` | shell 调用 nm/readelf，复杂集合比较由小型 C++ 审计工具处理 |
| `script/check-memory-headers.py` | shell/C++ 检查头文件与目录边界，读取集中配置的源码根 |
| `script/audit-service-functions.py` | C++ 词法审计，保留现有类方法/允许入口边界，不能用简单 grep 降低检查能力 |
| `script/smoke-hybrid.py` | shell 编排隔离镜像、QEMU、超时、日志与标记检查；镜像二进制处理交给 C++ 工具 |
| `script/smoke-desktop-input.py` | shell/C++ 测试驱动，保留实际输入注入和结果验证 |
| `tests/package-cache.py` | C++/shell 包缓存、预检与失败恢复测试，并扩展 upgrade 用例 |
| `build/` 内历史一次性 Python 工具 | 核对用途；仍必需的能力迁为正式 shell/C++ 工具，过期脚本及副本清理 |
| SDK、包暂存目录中的 Python 副本 | 停止导出，清理旧副本与相关缓存；重新生成 SDK 和受影响包 |

除根目录 clang-format.py 外，清理 `.py`、`.pyc`、`__pycache__` 及其他可执行 Python 脚本；排查 shebang、shell 内嵌 Python、C++ 子进程调用和构建依赖。格式化脚本不得成为普通构建/测试的隐式依赖。

不将历史日志中提及 Python 的文本当作可执行依赖，也不删除与任务无关的用户数据；清理前给出具体文件清单，按适用授权规则执行。只读架构文件不因其中提及旧脚本而改写。

## 6. 工具职责和自举顺序

- inpkg：包格式、校验、导入、包元数据输出、安装/upgrade/卸载和数据库。
- osinfo：项目配置、源码编译与链接；pkg 构建完成后调用 Linux C++ inpkg，移除自身重复的归档/包规则。
- OS 宿主工具：集中配置生成、SDK 导出、镜像布局、源码审计。
- shell：依赖顺序、挂载/卸载、调用宿主工具、测试编排；业务清单读取集中配置或生成产物。

Linux inpkg 只依赖宿主 C++ 工具链及明确的宿主依赖，不依赖 OSInfo SDK、目标 inpkg 或 osinfo pkg。OS 配置/SDK工具也必须能用宿主工具链构建。目标 inpkg 的包由 Linux inpkg 生成，避免构建循环。

## 7. 完整实施流程

1. 核对关联项目的规则、已有改动和 Python/配置使用点，建立消费者对照表及迁移清单。
2. 拆分 inpkg 公共实现和平台适配，先完成 Linux 可执行文件；实现 upgrade 与 install-missing 及事务测试。
3. 落实集中配置、校验器和生成产物；验证服务表、启动依赖、目录权限和应用/包身份与当前系统一致。
4. 将主仓库包构建、导入及 osinfo pkg 切到 C++ inpkg。更新 SDK 导出与布局工具，移除 Python 兼容路径。
5. 迁移审计、测试驱动及有用的辅助工具；每项替换保留原检查能力，失败不得被静默跳过。
6. 从全新输出目录自举宿主工具、OSInfo、SDK、基础工具、guiw 以及目标 inpkg；使用 Linux inpkg 导入包。
7. 镜像流程读取集中配置，先部署内核/服务/解释器/版本等系统文件，再对包集执行 `upgrade --install-missing`。该选项只解决首次安装与重复同步，其他冲突照常报错。
8. 对旧 SDK、构建目录、暂存包和缓存执行有清单的迁移/清理，确保普通构建不再生成 Python 文件。
9. 运行完整回归，更新各项目文档及实施记录，确认架构文件无差异。

预期开发使用方式如下；命令的批量参数与配置字段在实现时统一，不保留多套入口：

```sh
make -C ../inpkg host
make -C ../infoide
make host-tools
make sdk
make -C ../osinfo-base-tools package
make -C ../guiw
make -C ../inpkg package

../inpkg/build/linux/inpkg import ../osinfo-base-tools/build/packages/osinfo-base-tools.inpk --package-dir packages/external
../inpkg/build/linux/inpkg import ../guiw/build/packages/guiw.inpk --package-dir packages/external
../inpkg/build/linux/inpkg import ../inpkg/build/packages/inpkg.inpk --package-dir packages/external

make all
# 以下挂载/安装根必须由调用者明确指定，镜像挂载仍由镜像流程管理。
../inpkg/build/linux/inpkg upgrade packages/external/osinfo-base-tools.inpk --sysroot /mnt/osinfo --install-missing
```

以上手工导入命令用于展示流程；日常自动化按 packages.conf 选定包集，不能再把三份包名复制到每个 shell。

## 8. 验收条件

- 两个平台都使用同一套包解析与安装语义，包格式兼容；Linux 安装结果能由 OSInfo inpkg 正确查询、升级和卸载。
- 覆盖首次 install、已安装 upgrade、未安装 upgrade 拒绝、install-missing、同版本重装、新增/删除文件、冲突、保留修改、锁与失败恢复。
- 损坏归档或数据库错误不能触发 install-missing；安装前完成归档完整性、清单、权限和目标路径验证。
- Linux sysroot 内全部操作有边界；拒绝宿主根、符号链接逃逸和越界目标。用户、系统和宿主三个身份不能混用。
- 系统保护路径继续由系统部署流程管理，任何包升级参数都不能绕过保护。
- 构建、镜像复制、服务启动、布局和 smoke 使用集中配置或生成产物；增删一项不要求在多个脚本重复手改。
- 全新构建与正常 make check/smoke 不依赖 Python。正式源码、SDK、包负载及构建目录全量扫描后，只允许根目录 clang-format.py 这一项例外。
- shell 通过语法检查，C++ 工具编译检查通过；保留内存、IPC、单核/多核、任务生命周期、桌面输入及动态链接回归。
- 文档给出最终命令、配置格式、自举流程、升级与恢复语义；不修改架构文件，不虚报崩溃原子性或未执行的测试。

用户已要求生成执行 Prompt 后直接开始实现，不再停留在大纲阶段。
