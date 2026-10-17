#include "../ext3.h"
#include "internal.h"
#include "os/abi.h"

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
