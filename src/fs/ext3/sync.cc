#include "service/fs/ext3/filesystem.h"
#include "service/fs/ext3/internal.h"
#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>

namespace fs::ext3 {

using namespace detail;

bool Filesystem::sync() {
    superblock_.state = 1;
    return writeSuperblock() && device_->flush();
}

bool Filesystem::readOnly() const {
    return device_ == nullptr || device_->readOnly();
}

}  // namespace fs::ext3
