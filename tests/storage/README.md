# v0.1.5 storage 测试与前置检查

## 发布验收

`release-filesystem.py BASELINE TAG` 使用通过验证的双核探针镜像目录与对应 SDK，构建独立 BootImage 根卷和一次性 VirtIO 附盘。两个独立进程写入并同步文件，探针验证忙时拒绝卸载、成功卸载以及重新挂载后的数据。

新隔离目录使用 `python3 tests/storage/m6_verify.py --tag v015 --version 0.1.5` 重建和运行 SDK、硬盘与 ISO 验证。随后用 `release-artifacts.py` 读取普通启动的 IMG/ISO，核对嵌入的版本和运行库、包构建目录与 SDK，并生成 SHA-256 清单。两个发布辅助脚本通过 `linux_m0_env.sh` 运行。

遇到失败时，保留同次构建的镜像和串口证据。可对已有镜像用 `runtime_baseline.py --run --case disk-persist --case iso-create` 定向复测，沿用准备时的版本、镜像名、CPU 数、用例模式和设备参数。已有通过记录须属于同一配置和基线。报告单独保存每次宿主等待时间，调整 `--seconds` 后可继续同配置复测。重复用例的旧日志以 `.attempt-N.log` 保留，只有完整矩阵全部通过才输出 `PASS`。可运行 `python3 tests/storage/runtime-baseline-selftest.py` 验证合并规则。

## M6 联合验证

在已准备 M0 私有工具链的新 Linux 隔离副本内运行 `python3 tests/storage/m6_verify.py --tag m6`。入口编译并运行五组主机用例，配置 GCC 14，构建、校验并导出 SDK，运行双核硬盘与 ISO 探针、真实 VirtIO 超时统计和单核普通启动。应使用未占用的构建名及默认 `build` 路径。汇总与命令日志写入 `tests/output/v0.1.5/M6/<tag>/`，测试镜像仍由 `runtime_baseline.py` 保存在测试输出内。

## M5 请求生命周期

`request-lifecycle.cc` 使用生产槽位实现验证截止时间、槽满、无效和重复完成、冻结后的迟到完成及环索引回绕。SDK 运行探针会故意填满异步完成端点，随后检查 storage 仍可处理同步请求。`virtio-timeout.py` 使用本阶段已通过的 `result.json`，在一次性 QEMU 镜像上额外接入可暂停读请求的 16 MiB 磁盘。测试要求观察到超时、内核确认停止、DMA 域回收以及其他设备继续工作。

联合入口给真实 VirtIO 超时测试传入 180 秒宿主等待预算，并为控制进程和清理留出额外时间。故障注入结果分别记录 `host_timeout_stage` 和来宾失败标记。

`lifecycle-lab.py` 的 `instrument` 入口仅接受 `/dev/shm/osinfo-m5lab.*` 隔离副本，在该副本插入两份 `lifecycle-lab-*.inc` 及物理页断言。构建该副本后，用 `run --build BUILD --baseline 已生成的M5镜像目录 --output 新目录` 启动专用 ISO。三块一次性盘分别覆盖复位失败、停机等待超时和任务退出时的活动域。探针等 `control.init` 就绪后才注入故障，以免把引导中断混入服务替换的验收。

## M4 刷新与完成进度

`flush-contract.cc` 使用实际的缓存决策、父设备顺序控制和 IPC 回包实现，覆盖未确认进度、部分读写及首次失败后停止拆批。在 Linux 使用 M1 的编译命令，改用 `tests/storage/flush-contract.cc` 并加 `-pthread`。SDK 运行探针同时检查 `flushStatus()` 和原有 `flush()`。

## M3 能力和分区回归

主机测试补充了 VirtIO sector 换算、IPC 批次上限、相邻和重叠分区、GPT 表越界与溢出。运行回归时可在现有 `runtime_baseline.py` 命令后加 `--extra-4kn`，给硬盘启动额外接入一块一次性 16 MiB 4Kn 设备。探针检查 4096 字节块与正确容量、零传输上限及读请求被拒绝，同时验证原 512 字节根卷可用。ISO 回归仍使用 BootImage。

## M1 后端契约测试

在 Linux x86-64 的仓库根目录编译和运行：

```sh
mkdir -p tests/output/v0.1.5/M1
g++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Iinclude/os-c++ \
  tests/storage/block-contract.cc -o tests/output/v0.1.5/M1/block-contract
tests/output/v0.1.5/M1/block-contract
```

测试直接使用生产内部头 `block_backend.h` 的几何和分区校验。内存夹具检验读写、刷新、停止及超时后的完成计数示例，不会启动 OSInfo，也不验证真实 VirtIO、IPC 或 DMA。

## M0 基线

所有脚本使用 Python 3.9+ 标准库，无需 pip。默认前置检查只读，不下载、安装、挂载、格式化或启动虚拟机。

```sh
python3 tests/storage/m0_baseline.py --profile host
python3 tests/storage/m0_baseline.py --profile host --run-host
python3 tests/storage/preflight-selftest.py
python3 tests/storage/m0_baseline.py --profile runtime --record
```

Windows 可用实际 Python 路径替代 python3；主机基线需要 PATH 中的 g++，也可用 `--cxx` 指定编译器可执行文件。它验证宿主环境中的算法，不表示 MinGW 能构建 OSInfo 系统。

- `partition-baseline.cc` 直接包含当前生产 `partition.h` 和公共 `os/storage.h`，使用测试内存盘执行解析。只为 Part 值对象的已用访问器提供宿主定义，没有替换生产解析器。
- 13 项用例覆盖已知 CRC32 向量、MBR 正常/末块/越界/零长/输出容量/签名/读失败、GPT 正常/头CRC/表CRC/逆序范围。
- 它不验证 IPC、设备生命周期、4Kn、分区重叠或整个 FS，也不把当前无效分区处理策略解释成未来 M1 已确认契约。
- 自检验证缺编译器返回 BLOCKED/2、默认检查不写产物及非法参数组合被拒绝。
- `--run-host` 或 `--record` 才会在 `tests/output/v0.1.5/M0/` 写 JSON、编译产物和完整命令输出。不要将输出目录当作测试源码。
- 返回码：0=PASS，1=FAIL，2=BLOCKED/无效参数；实际详细原因见输出和 JSON。
- JSON 记录当前相关源码摘要、调用方路径、单位关键字位置和二进制包输入摘要；这只是可复核清单，不表示所有内容已经通过运行验证。
- runtime 检查会实际执行 inpkg 的 --help、infoide 的 --version；有 ZIP 输入时检查仓库 unzip 的 -v。具有执行权限不等于宿主 ABI 和动态库兼容，启动失败也返回 BLOCKED。

## 历史测试来源

Git 历史提交 `6f8f438` 删除了 tests/；父提交中保留过测试。已只读确认 `6f8f438^:tests/smoke-hybrid.sh`，未整体恢复到当前工作区，也未运行旧脚本。

当前源码相对旧测试的接口和目录已大幅变更，原始运行测试需要恢复、审计和适配。新主机基线不替代原来的内存、IPC、生命周期及系统运行验收。

## 系统基线前置条件

生产 configure 要求 Linux。2026-11-03 已通过用户提供的 SSH 在 openEuler 24.03 LTS-SP4 隔离副本中完成主机测试（13/13）和自检（3/3）；GCC 为 12.3.1。

初始环境缺项：inpkg 需要 GLIBCXX_3.4.31，infoide 需要 GLIBCXX_3.4.32，系统 libstdc++ 仅提供到 3.4.30；仓库 unzip 需要 libbz2.so.1.0 名称。QEMU 与常见路径中的 GRUB BIOS 模块也未找到。续作已在测试目录准备私有依赖，并实测 configure 和 QEMU 启动通过。

在用户授权的隔离 Linux 副本中，可显式运行以下准备步骤（这会下载官方包；默认前置检查仍不下载）：

```sh
python3 tests/storage/prepare_linux_dependencies.py
sh tests/storage/linux_m0_env.sh python3 tests/storage/m0_baseline.py --profile runtime --build-dir build-m0-final --package-dir packages-m0-final --record
```

准备脚本只解包至 tests/output，Debian 包固定 SHA-256，openEuler RPM 须通过现有信任库的签名检查。宿主限定为本次 openEuler x86_64 环境；私有 bzip2 链接依赖已有 `/usr/lib64/libbz2.so.1.0.8`，不能作为通用 Linux 安装器。调用者须在隔离副本使用，不将下载物加入生产软件包。

最新状态：虚拟机恢复后确认旧输出存在零字节对象；已在全新的 build-m0-final、obj-m0-final、packages-m0-final 中，用私有 GCC 14 完成 make all check sdk。SDK 251 个文件摘要及 3 个核心运行库与部署输入一致；VirtIO 文件写入/刷新/读回、重启持久性、ISO BootImage 读写均通过。旧 GCC 12 内部错误与损坏产物保留作历史证据。

`runtime-baseline.cc` 已通过目标 SDK 编译和实际系统执行，覆盖设备枚举、7 块读取、容量边界拒绝、flush、根 FS 的 12293 字节写入/fsync/重新打开/长度与内容比对。只部署到一次性镜像。Windows MinGW 的独立语法检查受 LLP64 与目标 LP64 ABI 差异阻塞（IpcMessage 大小断言），没有为适配 Windows 修改生产 ABI。

当前 Makefile 的 check 不调用历史 tests/*.sh。前置检查现已检查当前 runtime_baseline.py 和 runtime-baseline.cc，并在 JSON 单独保留旧内存、IPC、生命周期故障覆盖未恢复的事实；前置检查 PASS 不代表运行测试 PASS。

## 复现系统构建与运行基线

在全新的 Linux 隔离副本中，先校验源码，按前述步骤适配 CRLF 并准备私有依赖，再复制原始 packages/ 顶层输入到新的 packages-m0-final/。不能沿用突然关机产生的对象；也不要删除其他人的输出。

```sh
private="$PWD/tests/output/v0.1.5/M0/linux-deps/sysroot"
sh tests/storage/linux_m0_env.sh env CC="$private/usr/bin/x86_64-linux-gnu-gcc-14" CPP="$private/usr/bin/x86_64-linux-gnu-g++-14" ./configure --build-dir build-m0-final --object-dir obj-m0-final --package-dir packages-m0-final
sh tests/storage/linux_m0_env.sh make CONFIGURE_BUILD=build-m0-final -j2 all check sdk
sh tests/storage/linux_m0_env.sh python3 tests/storage/runtime_baseline.py --name runtime-new --prepare --run
sh tests/storage/linux_m0_env.sh python3 tests/storage/runtime_baseline.py --name normal-new --normal --prepare --run
```

输出目录名须未使用过，保留失败日志。脚本核对 SDK 全量摘要，再用 SDK 编译探针；镜像沿用生产 install、layout、GRUB 嵌入逻辑，模块路径取自测试私有目录，不挂载镜像、不使用 sudo、不触及宿主块设备。--normal 保留原始登录会话；默认只在镜像中替换登录程序执行探针。

SDK 链接修复：正式 script/user.lds 已显式将 GNU property、build-id 和其他 note 分配到 text 段，避免继承 PT_INTERP。测试入口直接使用导出的 SDK 链接脚本，已移除此前复制脚本并丢弃属性节的绕过措施。check_sdk_elf.py 核对解释器段必须恰好为路径及末尾 NUL，note 不得与它重叠。运行时初始化会消费 --os-terminal 参数，探针通过公共 Task 信息选择 tty1，避免多个会话同时写文件。

QEMU 使用 TCG、512 MiB、2 vCPU。成功条件以串口标记与内容校验为准，达到标记后终止模拟器；不把宿主超时当成功。ISO 根卷写入只验证运行期可读写，磁盘重启验证只覆盖本次文件和 fsync，不证明突然掉电原子性或普遍持久保证。GUI 交互和历史内存/IPC 故障用例不在本轮通过清单中。

## 固定工具链与 SDK 链接回归

本机系统 openEuler GCC 12.3.1-110 的安装校验正常，但同一源文件可成功、全量构建又会在另一文件内部崩溃。原异常尚不能归因为某行源码；不添加重试循环或关闭优化掩盖问题。独立 Debian GCC 12.2.0-14+deb12u1 和 GCC 14.2.0-19 均已通过全量构建，推荐在本验证环境使用固定工具链入口。

```sh
# GCC 12 对照工具链为可选下载，仍只解包到测试目录。
python3 tests/storage/prepare_linux_dependencies.py --with-gcc12
python3 tests/storage/configure_private_toolchain.py --compiler gcc12 --tag my-gcc12
sh tests/storage/linux_m0_env.sh make CONFIGURE_BUILD=build-my-gcc12 -j2 all check sdk
```

默认选择 gcc14，亦可使用 `--compiler gcc14`。新 tag 会分离 build/obj/packages，已有输出一律拒绝复用；不会更改系统 gcc/g++、共享库、PATH 配置或其他成员的目录。configure 本身仍支持原 CC/CPP 覆盖，不对所有 GCC 12 或所有 openEuler 版本作无依据的禁用。

探针编译器可用 `runtime_baseline.py --cxx x86_64-linux-gnu-g++-12` 指定，配合 --build 和 --packages 验证 GCC 12 SDK。已导出的旧 SDK 需重新执行 make sdk，随后重编应用；源码中链接脚本修复不会自动改变旧 ELF。

单独检查实际 SDK 应用：`python3 tests/storage/check_sdk_elf.py APP`。真实修复前 ELF 会失败（退出 1），修复后解释器段为 25 字节，属性 note 仍保留。该检查不能替代系统启动测试。

隔离快照由 `prepare_linux_baseline.py` 生成，排除 Git、测试输出及链接；它会包含允许目录内的文件，不作为通用保密信息检查器。传输前核对内容，传输后校验归档 SHA-256 与逐文件摘要。Windows 检出脚本为 CRLF，本次只在 Linux 副本中转换 configure 和 script/*.sh 的换行，并以摘要记录；本地生产脚本未改。GRUB 命令差异只通过副本测试目录内的 grub-file 链接适配。

Linux 证据保存于 `tests/output/v0.1.5/M0/remote/linux-evidence/`，与 Windows 原结果分开；含环境、配置失败、前置检查和副本差异记录。准备脚本只打包，不自动连接 SSH、安装软件或启动镜像。

准备完成后先重新执行 runtime 前置检查，再在独立构建目录生成匹配产物和隔离镜像，完成硬盘、ISO 内存根、FS 读写与 SDK 基线。当前脚本不会自动执行这些构建或启动步骤。
