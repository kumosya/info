#pragma once
#include "kernel/ipc.h"

namespace kernel {
class ServiceRecovery {
   public:
    static void initialize();
    static int call(Task& caller, const IpcMessage& request, IpcMessage& reply);
    static bool current(const Task& caller);
};
}
