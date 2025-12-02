#include <entry.h>
#include <multiboot2.h>
#include <page.h>

#include <stdint.h>

namespace boot::mm {
    // 全局页框管理实例
    frame::mem pm;

    namespace frame {
        // 初始化物理内存管理
        void init(uint64_t start_addr, uint64_t end_addr) {
        }

        // 分配一个物理页框
        void* alloc() {
        }

        // 释放一个物理页框
        void free(void* addr) {
        }
    }

    // 页表管理命名空间
    namespace paging {
        // 初始化页表
        void init() {
            boot::printf("Paging init\n");
        }

        // 虚拟内存映射函数
        void mapping(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
        // 使用page.h中定义的宏来计算页表索引
        }

        // 映射内核段
        void mapping_kernel(uint64_t* pml4) {}
    }

    void init(uint8_t *addr) {
        boot::printf("MM init\n");

        
        return;
    }
}
