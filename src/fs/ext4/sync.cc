#include "service/fs/ext4/filesystem.h"
#include "service/fs/ext4/internal.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace fs::ext4 {

using namespace detail;

bool Filesystem::readOnly() const {
    return device_ == nullptr || device_->readOnly() || !writeSupported_;
}

bool Filesystem::sync() { return device_ != nullptr && device_->flush(); }

}  // namespace fs::ext4
