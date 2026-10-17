#include "../ext4.h"
#include "internal.h"
#include "os/abi.h"

namespace fs::ext4 {

using namespace detail;

bool Filesystem::readOnly() const {
    return device_ == nullptr || device_->readOnly() || !writeSupported_;
}

bool Filesystem::sync() { return device_ != nullptr && device_->flush(); }

}  // namespace fs::ext4
