# 内核启动（src/kernel/boot/）

本文档描述 `src/kernel/boot/` 的实现：Multiboot2 引导头、32→64 位引导跳板（`head.S`）、Multiboot2 信息解析（`multiboot.cc`）与串口驱动（`serial.cc`，位于 `src/kernel/device/`）。对应头文件 `include/kernel/boot.h`、`include/kernel/serial.h`。

## 1. Multiboot2 引导头（head.S `.multiboot` 节）

引导头 8 字节对齐，字段：

| 字段 | 值 | 说明 |
|---|---|---|
| magic | `0xe85250d6` | Multiboot2 魔数 |
| arch | `0` | i386（32 位保护模式入口） |
| header_length | 头实际长度 | |
| checksum | `-(magic + arch + length)` | |

标签（tag）：

- **Tag 1（信息请求，必需）**：请求 tag 8（framebuffer 信息）。
- **Tag 5（framebuffer，可选）**：请求 1024×768×32 bpp 图形模式。
- **Tag 0（结束）**。

## 2. 32 位引导流程（`_start`，`.code32`）

GRUB 以 32 位保护模式进入 `_start`，`eax = 0x36d76289`（Multiboot2 引导魔数），`ebx` = Multiboot 信息结构物理地址。

流程：

1. `cli`；保存 `ebx` 到 `multibootAddress`。
2. 校验 `eax == 0x36d76289`，否则跳 `bootFailure`（向 COM1 输出 `'B'` 后 `cli; hlt`）。
3. 设置引导栈 `esp = bootStackTop`（16 KB 静态栈）。
4. `checkLongMode`：
   - 用 EFLAGS ID 位（bit 21）翻转测试 CPUID 可用性；
   - CPUID leaf 1：要求 SSE（EDX bit 25）与 SSE2（EDX bit 26）；
   - CPUID `0x80000000`：最大扩展叶 ≥ `0x80000001`；
   - CPUID `0x80000001`：要求 Long Mode（EDX bit 29）与 NX（EDX bit 20）。
   - 任一失败跳 `bootFailure`。
5. `setupPageTables`（见下）。
6. `lgdt gdtPointer`。
7. CR0：清 EM（bit 2）、置 MP（bit 1）。
8. CR4：置 PAE（bit 5）、OSFXSR（bit 9）、OSXMMEXCPT（bit 10）。
9. CR3 = `pageMapPml4`。
10. EFER MSR（`0xc0000080`）：置 LME（bit 8）与 NXE（bit 11）。
11. CR0 置 PG（bit 31），`ljmp $0x10, $start64` 进入 64 位。

### 引导页表（`setupPageTables`）

- 清零 66 个页表页：1 个 PML4 + 1 个 PDPT + 64 个 PD。
- `PML4[0]` 与 `PML4[256]` 都指向同一个 PDPT（标志 `0x3`）——低端恒等映射与高半直接映射（`0xffff800000000000`）双重映射。
- `PDPT[0..63]` 指向 64 个页目录。
- 填充 64×512 = 32768 个 **2 MB 大页**表项（标志 `0x83` = Present|Writable|PS），恒等映射前 64 GB 物理内存。

### 引导 GDT（4 项）

| 选择子 | 内容 | 含义 |
|---|---|---|
| 0x00 | 0 | 空 |
| 0x08 | `0x00cf9a000000ffff` | 32 位代码段 |
| 0x10 | `0x00af9a000000ffff` | 64 位代码段（L=1） |
| 0x18 | `0x00af92000000ffff` | 64 位数据段 |

## 3. 64 位入口（`start64`，`.code64`）

1. `cld`、`fninit`。
2. `ds/es/ss = 0x18`。
3. `rsp = bootStackTop + 0xffff800000000000`（切换到高半栈）。
4. 传参调用 `kernelEntry`：
   - `edi` = Multiboot 信息物理地址；
   - `rsi` = CR3（页表根物理地址）；
   - `rdx` = `__bootStart`（引导映像起始，链接脚本符号）；
   - `rcx` = `__kernelLoadEnd`（内核加载结束）。
5. `kernelEntry` 返回后 `cli; hlt` 死循环。

`kernelEntry`（`multiboot.cc`）先 `kernel::serial::initSerial()`，再 `kernel::parseMultibootInfo`，把 `pageTableRoot`、`bootstrapStart`、`bootstrapEnd` 填入 `BootInfo`，最后进入 `kernelMain`（见 [overview.md](overview.md) 的初始化序列）。

## 4. Multiboot2 信息解析（multiboot.cc）

### BootInfo 结构（boot.h）

```cpp
namespace kernel {

struct BootInfo {
    const MemoryRegion* memoryMap;   // 内存映射数组
    uint32_t memoryMapCount;
    const BootModule* modules;       // 引导模块数组
    uint32_t moduleCount;
    const char* commandLine;
    const char* bootLoaderName;
    const void* acpiRoot;            // RSDP 指针
    const void* elfSections;
    FramebufferInfo framebuffer;
    uint64_t multibootAddress;
    uint32_t multibootSize;
    uint32_t reserved;
    uint64_t pageTableRoot;          // 由 kernelEntry 填入
    uint64_t bootstrapStart;         // 由 kernelEntry 填入
    uint64_t bootstrapEnd;           // 由 kernelEntry 填入
};

}  // namespace kernel
```

辅助结构：

```cpp
struct MemoryRegion { uint64_t base; uint64_t length; uint32_t type; };
struct BootModule { uint32_t start; uint32_t end; const char* commandLine; };
struct FramebufferInfo {
    uint64_t address; uint32_t pitch, width, height;
    uint8_t bitsPerPixel, type;
    uint8_t redPosition, redMaskSize, greenPosition, greenMaskSize,
            bluePosition, blueMaskSize;
};
```

tag 类型常量：`multibootTagEnd=0`、`multibootTagBootCommandLine=1`、`multibootTagBootLoaderName=2`、`multibootTagModule=3`、`multibootTagMemoryMap=6`、`multibootTagFramebuffer=8`、`multibootTagElfSections=9`、`multibootTagAcpiOld=14`、`multibootTagAcpiNew=15`；`memoryAvailable = 1`。

### 解析规则 `parseMultibootInfo(uint64_t address, BootInfo*)`

1. 结构总大小必须落在 `[16, 16 MB]`，否则失败。
2. 从 `address + 8` 起按 8 字节对齐遍历 tag；每个 tag 校验 `size >= 8` 且不越界。
3. 处理的 tag：
   - **1（命令行）** → `commandLine`；
   - **2（引导加载器名）** → `bootLoaderName`；
   - **6（内存映射）**：校验 `entrySize >= 24`，逐条复制（最多 128 条）；
   - **3（模块）**：最多 32 个；取 start、end 与命令行字符串；
   - **8（framebuffer）**：复制全部字段；仅当类型为 1（RGB）时复制颜色掩码；
   - **9（ELF 节）** → `elfSections`；
   - **14/15（ACPI RSDP）** → `acpiRoot`；
   - **0（结束）**：退出循环。
4. 所有指针都换算为直接映射虚拟地址（`物理地址 + 0xffff800000000000`）。
5. 若最终没有内存映射（`memoryMapCount == 0`），打印 `[BOOT] memory map tag missing` 并返回 false。

静态存储：`memoryRegions[128]` 与 `bootModules[32]` 为匿名命名空间静态数组，`BootInfo` 指向它们。

## 5. 串口驱动（serial.cc）

COM1（`0x3f8`），8250/16550，115200 波特、8 数据位、无校验、1 停止位。

所有函数位于 `namespace kernel::serial`：

`initSerial()` 初始化序列：

1. IER（+1）= 0x00（关中断）；
2. LCR（+3）= 0x80（使能 DLAB）；
3. DLL（+0）= 0x01、DLM（+1）= 0x00（分频 1 → 115200）；
4. LCR = 0x03（8N1）；
5. FCR（+2）= 0xc7（使能并清空 FIFO，14 字节触发）；
6. MCR（+4）= 0x0b（DTR + RTS + OUT2）。

输出接口（`serial.h`）：

```cpp
namespace kernel::serial {

void initSerial();
void serialWrite(char value);          // 轮询 THR 空，最多 100000 次
void serialWrite(const char* value);   // \n 自动转 \r\n
void serialWriteHex(uint64_t value);   // 0x 前缀 + 16 位十六进制
void serialWriteDecimal(uint64_t value);

}  // namespace kernel::serial
```

串口是内核唯一的早期诊断通道，在帧缓冲控制台可用之前以及内核 panic 路径上使用。
