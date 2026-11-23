/*
 * 识别可归因于程序的异常，并转换为任务信号。
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/task.h"

namespace cpu {

    /*
     * 按约定优先级从浮点待处理位中挑选错误类别，转为用户可见的浮点信号原因。
     */
    static int floatingCode(uint32_t pending) {
        // 按固定次序选择浮点错误：无效运算、除零、上溢、下溢、精度，最后处理非规格化情况。
        if (pending & 1) {
            return 7;
        }

        if (pending & 4) {
            return 3;
        }

        if (pending & 8) {
            return 4;
        }

        if (pending & 16) {
            return 5;
        }

        if (pending & 32) {
            return 6;
        }

        return 5;
    }

    /*
     * 把能够归因到用户执行的 CPU 异常转换成进程信号。
     * 先筛选支持的异常和现场来源，再补充故障地址、调试或浮点信息；其余异常交给内核严重错误路径。
     */
    bool dispatchUserException(Context* context) {
        // 先限制可转成进程信号的异常种类；平台故障和保留异常不能只凭保存的 CS
        // 就归咎于用户程序。
        switch (context->vector) {
            case 0:
            case 1:
            case 3:
            case 4:
            case 5:
            case 6:
            case 12:
            case 13:
            case 16:
            case 17:
            case 19:
                break;
            default:
                return false;
        }

        // 除真正的用户态现场外，还兼容在用户地址范围执行的 Sys
        // 线程；其他内核现场交给严重故障路径。
        ::task::Thread* current = ::task::currentThread();
        const bool serviceInstruction =
            context->rip >= 0x10000 && context->rip < 0x800000000000ULL &&
            current && current->getType() == ::task::ThreadType::Sys;

        if ((context->cs & 3) != 3 && !serviceInstruction) {
            return false;
        }

        // 分别记录信号、具体原因及故障地址；只有确实取得有效地址时才设置
        // valid。
        uint32_t signo   = 0;
        int code         = 128;
        uint64_t address = 0, debug = 0;
        bool valid = false;

        switch (context->vector) {
            case 0:
                signo   = 8;
                code    = 1;
                address = context->rip;
                valid   = true;
                break;
            case 1:

                // 调试状态说明是单步还是硬件断点；读取后复位
                // DR6，防止旧状态影响下一次调试异常。
                asm volatile("movq %%dr6, %0" : "=r"(debug));
                asm volatile("movq %0, %%dr6" : : "r"(0xffff0ff0ULL));
                signo   = 5;
                code    = (debug & (1ULL << 14)) ? 2 : (debug & 15) ? 4 : 128;
                address = context->rip;
                valid   = true;
                break;
            case 3:
                signo   = 5;
                code    = 1;
                address = context->rip;
                valid   = true;
                break;
            case 4:
                signo   = 8;
                code    = 2;
                address = context->rip;
                valid   = true;
                break;
            case 5:
                signo = 11;
                code  = 3;
                break;
            case 6:
                signo   = 4;
                code    = 1;
                address = context->rip;
                valid   = true;
                break;
            // TSS、浮点未就绪、CET 和平台异常需要专门的架构处理规则，不能只凭
            // CS 认定为用户错误。 当前不支持 LDT
            // 或不存在的用户段，因此段不存在异常意味着描述符或投递链路损坏。
            case 11:
                return false;
            case 12:
                if (context->errorCode) {
                    return false;
                }

                signo = 11;
                break;
            case 13:
                if (context->errorCode & 1) {
                    return false;
                }

                signo = 11;
                break;
            case 16:
            case 19: {
                ::task::Thread* thread = ::task::currentThread();

                if (!thread) {
                    return false;
                }

                // 先保存当前浮点现场，再从 x87 或 SSE
                // 状态里挑出尚未屏蔽的错误，不能把已屏蔽状态当作故障。
                thread->saveFloatingPointState();
                const uint8_t* fp = thread->fpState();
                const uint32_t pending =
                    context->vector == 16
                        ? (*reinterpret_cast<const uint16_t*>(fp + 2) &
                           ~*reinterpret_cast<const uint16_t*>(fp)) &
                              63
                        : (*reinterpret_cast<const uint32_t*>(fp + 24) &
                           ~(*reinterpret_cast<const uint32_t*>(fp + 24) >>
                             7)) &
                              63;

                if (!pending) {
                    return false;
                }

                signo   = 8;
                code    = floatingCode(pending);
                address = context->rip;
                valid   = true;
                break;
            }

            case 17:
                signo = 7;
                code  = 1;
                break;
            default:
                return false;
        }

        // 将归一化后的故障信息交给信号机制，后续在返回用户态前决定投递或结束任务。
        task::raiseException(context, signo, code, address, valid,
                             ::task::FaultAccess::Unknown, debug);

        return true;
    }

}  // namespace cpu
