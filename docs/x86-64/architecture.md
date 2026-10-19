# 内核架构

所有服务进程运行在 Ring3：init、task、fs、pci、device、storage、tty、console、guiw、users、MM、pipe、msgq。固定 boot 身份只授予受控机制调用权限，不改变进程运行级别。

Ring0 只保留基础内核机制及 task.kernel、pci.kernel、device.kernel、memory.kernel、system.kernel 等机制对象。服务进程不得直接操作 I/O 端口。PS/2 控制器和 PCI 配置端口属于内核硬件实现；Ring3 只发送受限设备协议。

~~~mermaid
flowchart TB
    subgraph R3["Ring 3 · 用户空间"]
        direction TB

        subgraph APP["应用层"]
            direction LR
            App["应用程序 / libc / libc++"]
        end

        subgraph API["OS API 层"]
            direction LR
            OS["OS Native API<br/>Process · Terminal · File · VirtualMemory<br/>Pipe · MessageQueue"]
            POSIX["POSIX API<br/>open · read · write · mmap · fork"]
        end

        subgraph SERVICES["用户态系统服务层"]
            direction TB
            
            Init["init"]
            Pci["pci"]
            TaskS["task"]
            FsS["fs"]
            DeviceS["device"]
            StorageS["storage"]
            TtyS["tty"]
            MemoryS["mm"]
            PipeS["pipe"]
            MsgqS["msgq"]
            ConsoleS["console"]
            GuiwS["guiw"]
            UsersS["users"]
        end
    end

    subgraph R0["Ring 0 · 内核空间"]
        direction TB

        subgraph KERNEL["内核接口与机制对象层"]
            direction LR

            subgraph ABI_GROUP["Native ABI"]
                direction TB
                ABI["Syscall 接口<br/>IPC · Handle · Mapping · Task"]
            end

            subgraph SERVICE_GROUP["Ring 0 机制对象"]
                direction TB
                TaskK["task.kernel"]
                MemoryK["memory.kernel"]
                DeviceK["device.kernel"]
                PciK["pci.kernel"]
                SystemK["system.kernel"]
            end
        end

        subgraph CORE["内核基础机制层"]
            direction LR
            Task["Task · Thread · Scheduler<br/>Signal · WaitQueue"]
            Memory["PMM · VMM · PageTable · TLB<br/>Buddy · Slab · Pool · Per-CPU · NUMA"]
            IPC["Endpoint · HandleTable<br/>Reply Token · Timeout · Cancel"]
            Device["IRQ · MMIO · DMA<br/>PCI Config · PS2 I/O"]
        end
    end

    subgraph HARDWARE["硬件层"]
        direction LR
        CPU["CPU / APIC / MMU"]
        RAM["Physical RAM"]
        HW["PCI / Storage / Devices"]
    end

    APP --> API
    API --> SERVICES

    API --> KERNEL
    SERVICES --> KERNEL

    ABI_GROUP --> CORE
    SERVICE_GROUP --> CORE

    CORE --> HARDWARE

    style R3 fill:#eaf6ef,stroke:#52a675
    style APP fill:#e8f4ff,stroke:#5793c9
    style API fill:#e8f4ff,stroke:#5793c9
    style SERVICES fill:#e9f8ee,stroke:#56a676

    style R0 fill:#f2eaff,stroke:#9169c5
    style KERNEL fill:#e8dcfa,stroke:#9169c5
    style ABI_GROUP fill:#dceaff,stroke:#5789c8
    style SERVICE_GROUP fill:#f0e3ff,stroke:#9169c5
    style CORE fill:#eee7f8,stroke:#9169c5

    style HARDWARE fill:#eeeeee,stroke:#888888
~~~

## 启动依赖

~~~mermaid
flowchart LR
  Kernel["内核及 boot 模块"] --> Init["Ring3 init"]
  Init --> PCI["Ring3 pci"]
  PCI --> Channels["Ring3 pipe / msgq"]
  Channels --> Services["Ring3 task / device / storage / fs"]
  Services --> MM["Ring3 MM"]
  MM --> Users["Ring3 users"]
  Users --> TTY["Ring3 tty"]
  TTY --> Console["Ring3 console"]
  Console --> GUI["Ring3 guiw"]
  GUI --> Sessions["GUI terminal + 六个 shell"]
~~~

普通磁盘 SpawnImage 不能申请 capabilities 或 ServiceRole。guiw 服务与终端客户端位于独立 `D:/osinfo/guiw` 项目的 `server/`、`src/`；inpkg 位于 `D:/osinfo/inpkg`。OS 仓库只接收并安装这两个项目的 `.inpk`，将 guiw 包中的引导镜像交给固定 WindowServer 角色。`user/Makefile` 是其余用户空间组件的独立构建入口。硬件权限按固定 boot 角色验证，不以 Ring0 执行或 root UID 作为服务身份。

普通磁盘程序通过 Ring3 `/lib/ld-osinfo-x86-64.so` 加载 `DT_NEEDED` 依赖。内核只验证、映射主 ELF 与 `PT_INTERP` 解释器；文件搜索、符号查找、重定位和共享库初始化不进入内核。文件系统可用之前的 boot 模块保留封闭的引导链接路径。宿主开发环境 Info IDE 位于 `D:/osinfo/infoide`，通过导出的 SDK 交叉编译和打包。

## RPC 请求状态

~~~mermaid
stateDiagram-v2
  [*] --> Queued: 校验权限及容量，捕获句柄引用
  Queued --> Claimed: 实际接收 Task 原子安装句柄
  Claimed --> Replied: 验证 Endpoint、Token 和领取进程
  Queued --> Cancelled: 超时、取消、信号或关闭
  Claimed --> Cancelled: 超时、取消或服务死亡
  Replied --> [*]
  Cancelled --> [*]: 唤醒调用者，拒绝迟到回复
~~~

pipe/msgq 延迟回复时查询请求是否仍有效，取消后释放所持能力引用。服务状态不放在内核。消息已领取后取消不回滚此前已完成的业务副作用；服务重启不自动重放原请求。

## SMP 旧地址空间回收

终止清理由专用内核 TaskReclaimer 执行，队列持有 Task 引用，请求者死亡不丢失回收责任。exec 的不可逆提交区禁止抢占，保持中断可用；并发 kill 必须等待提交线程实际切走后才能回收其地址空间。

~~~mermaid
sequenceDiagram
  participant A as exec CPU / 内核回收线程
  participant S as 调度器
  participant B as 远端 CPU
  participant M as 地址空间
  A->>S: 禁止新线程，终止其他线程并摘运行队列
  S->>B: 请求重新调度
  B->>B: 完成上下文及 CR3 切换
  B->>S: 更新 executingTasks 和已选中线程
  A->>S: quiesceTask 等待所有 CPU 离开
  S-->>A: 已停稳
  A->>M: 替换根并回收旧页表
~~~

Task 退出状态保留至 wait 回收。独立生命周期测试内核覆盖清理请求者被杀和跨 CPU exec/kill 交错。CPU 停稳不能替代 DMA 设备停止；设备死亡后的硬件停机与安全重启协议仍需完善。
