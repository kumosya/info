#pragma once
#include "kernel/boot.h"
#include "kernel/ipc.h"
namespace kernel::device {
class BootImage {
 public:
    static void init(const BootInfo& boot);
    static int call(const IpcMessage& request, IpcMessage& reply);
};
}
