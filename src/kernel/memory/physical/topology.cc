/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/physical/topology.cc
 * 读取固件内存布局，关联物理区间、CPU 与内存节点。
 */

#include "kernel/acpi.h"
#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"

namespace mm {

    static MemoryTopology topology;

    /*
     * 按小端顺序读取固件表中的整数。
     * 逐字节组合，避免依赖结构体对齐或直接读取未对齐地址。
     */
    static uint64_t readSratValue(const uint8_t* data, unsigned bytes) {
        uint64_t result = 0;

        for (unsigned i = 0; i < bytes; ++i) {
            result |= static_cast<uint64_t>(data[i]) << (i * 8);
        }

        return result;
    }

    /*
     * 返回全局内存拓扑对象，用来查询物理区间与内存节点的对应关系。
     */
    MemoryTopology& memoryTopology() {
        return topology;
    }

    /*
     * 把固件给出的域编号转换成内核使用的紧凑节点编号；首次遇到的域会占用一个节点槽。
     */
    uint16_t MemoryTopology::nodeForDomain(uint32_t domain) {
        for (uint16_t i = 0; i < nodes; ++i) {
            if (domains[i] == domain) {
                return i;
            }
        }

        if (nodes == maximumMemoryNodes) {
            return anyMemoryNode;
        }

        domains[nodes] = domain;

        return nodes++;
    }

    /*
     * 读取固件的 SRAT 表，收集内存区间和 CPU 所属节点。
     * 表缺失或条目不适用时保留默认节点，使单节点机器也能正常启动。
     */
    void MemoryTopology::init(const BootInfo* boot) {
        nodes      = 1;
        domains[0] = 0;
        rangeCount = cpuCount              = 0;
        cpuLocalReady                      = false;
        const ::acpi::AcpiSdtHeader* table = acpi::Tables(boot).find("SRAT");

        if (!table || table->length < sizeof(acpi::AcpiSdtHeader) + 12) {
            return;
        }

        // 用字节指针遍历固件表，条目长度由固件提供，每次前进都受整个表长度约束。
        const uint8_t* data = reinterpret_cast<const uint8_t*>(table);

        for (uint32_t offset = sizeof(acpi::AcpiSdtHeader) + 12;
             offset + 2 <= table->length;) {
            const uint8_t* entry  = data + offset;
            const uint32_t length = entry[1];

            if (length < 2 || length > table->length - offset) {
                break;
            }

            // 内存条目使用左闭右开区间；排除空区间和地址溢出，并独立检查区间记录容量。

            // 按条目类型分别解析内存亲和信息与处理器亲和信息，不能把它们当成同一种结构。
            if (entry[0] == 1 && length >= 40 &&
                (readSratValue(entry + 28, 4) & 1)) {
                const uint16_t node =
                    nodeForDomain(readSratValue(entry + 2, 4));
                const uint64_t first = readSratValue(entry + 8, 8),
                               size  = readSratValue(entry + 16, 8);

                if (node != anyMemoryNode && rangeCount < 64 && size &&
                    first + size > first) {
                    ranges[rangeCount++] = {first, first + size, node};
                }
            } else if (entry[0] == 0 && length >= 16 &&
                       (readSratValue(entry + 4, 4) & 1)) {
                // 旧式 APIC 条目把域编号拆在不同字节里，x2APIC
                // 条目则有自己的完整编号和固定偏移，不能共用一种解析布局。
                const uint32_t domain =
                    entry[2] | (readSratValue(entry + 9, 3) << 8);
                const uint16_t node = nodeForDomain(domain);

                if (node != anyMemoryNode && cpuCount < 256) {
                    cpus[cpuCount++] = {entry[3], node};
                }
            } else if (entry[0] == 2 && length >= 24 &&
                       (readSratValue(entry + 12, 4) & 1)) {
                const uint16_t node =
                    nodeForDomain(readSratValue(entry + 4, 4));

                if (node != anyMemoryNode && cpuCount < 256) {
                    cpus[cpuCount++] = {
                        static_cast<uint32_t>(readSratValue(entry + 8, 4)),
                        node};
                }
            }

            offset += length;
        }
    }

    /*
     * 查询某个物理地址所属节点，找不到记录时使用默认节点。
     */
    uint16_t MemoryTopology::nodeForAddress(uint64_t address) {
        for (uint32_t i = 0; i < rangeCount; ++i) {
            if (ranges[i].contains(address)) {
                return ranges[i].node;
            }
        }

        return 0;
    }

    /*
     * 标记 CPU 局部信息已经可用，此后可按实际 CPU 选择就近内存节点。
     */
    void MemoryTopology::enableCpuLocal() {
        __atomic_store_n(&cpuLocalReady, true, __ATOMIC_RELEASE);
    }

    /*
     * 取得当前 CPU 对应的内存节点；启动早期使用默认节点。
     */
    uint16_t MemoryTopology::currentNode() {
        if (!__atomic_load_n(&cpuLocalReady, __ATOMIC_ACQUIRE)) {
            return 0;
        }

        const uint32_t apic = cpu::getCurrent()->getApicId();

        for (uint32_t i = 0; i < cpuCount; ++i) {
            if (cpus[i].matches(apic)) {
                return cpus[i].node;
            }
        }

        return 0;
    }

    /*
     * 返回已识别的内存节点数量。
     */
    uint16_t MemoryTopology::nodeCount() {
        return nodes;
    }

}  // namespace mm
