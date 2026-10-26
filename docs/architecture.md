# 内核架构

所有服务进程运行在 Ring3。boot 身份只会授予受控机制调用权限，不改变进程运行级别。

Ring0 只有基础内核机制及 task.kernel、pci.kernel、device.kernel、memory.kernel、system.kernel 等机制对象。Ring3 服务进程是不能直接操作 I/O 端口的，只会向对应 Ring0 进程发送 I/O 请求。

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
            TaskS["task"]
            MemoryS["mm"]
            FsS["fs"]
            DeviceS["device"]
            TtyS["tty"]
            ConsoleS["console"]
            UsersS["users"]
            GuiwS["guiw"]
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

普通磁盘程序不能通过启动参数申请特权。 即使指定了 capabilities 或 ServiceRole，也不能借此把自己变成系统服务。否则，任意程序都可以申请访问硬件。

普通磁盘程序通过 Ring3 `/lib/ld-osinfo-x86-64.so` 加载 `DT_NEEDED` 依赖。内核只验证、映射主 ELF 与 `PT_INTERP` 解释器；文件搜索、符号查找、重定位和共享库初始化不进入内核。文件系统可用之前的 boot 模块保留封闭的引导链接路径。开发环境 Info IDE 的仓库在 [这里](https://github.com/kumosya/infoide)。infoide 通过由 osinfo 导出的 SDK 交叉编译和打包。

guiw 服务、osgui 库与图形界面的实现位于 [GUI Window](https://github.com/kumosya/guiw) 项目的 `server/`、`src/`；inpkg 位于 [这里](https://github.com/kumosya/inpkg)。
