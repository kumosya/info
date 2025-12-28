
#include "task.h"
#include "tty.h"
#include "mm.h"
#include "page.h"
#include "cpu.h"
#include "io.h"
#include "vfs.h"
#include "block.h"
#include <cstdint>
#include <cstring>

char *cmdline;

// 定义init函数，用于初始化系统核心功能
int SysInit(int argc, char *argv[]) {
    tty::printf("boot cmdline:%s\n", cmdline);

    // 创建block线程
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(block::Proc), "block", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(vfs::Proc), "vfs", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(tty::Proc), "tty", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(mm::Proc), "mm", 0);
    
    while (true) {
    }
    
    return 1;
}