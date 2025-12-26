
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

// 定义init函数，用于初始化系统核心功能
int SysInit(int argc, char *argv[]) {
    tty::printf("init task is running, argc: %d, argv[0]: %s\n", argc, argv[0]);

    // 创建block线程
    char **block_argv = (char **)mm::page::Alloc(2 * sizeof(char *));
    block_argv[0] = (char *)mm::page::Alloc(6); // "block" + null terminator
    strcpy(block_argv[0], "block");
    block_argv[1] = NULL;
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(block::Proc), block_argv, 0);
    mm::page::Free(block_argv[0]);
    mm::page::Free(block_argv);
    
    while (true) {
        for (int i = 0; i < 0xffffff; i++);
        tty::printf("I");
    }
    
    return 1;
}