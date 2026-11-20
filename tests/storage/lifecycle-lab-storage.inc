// Compiled only into the disposable lifecycle lab.
int lifecycleLab() {
    using Error = os::Storage::Error;
    using Stop  = storage::StopResult;
    uint32_t disks[3]{deviceCapacity, deviceCapacity, deviceCapacity};
    for (uint32_t i = firstVirtio_; i < driverCount_; ++i) {
        const auto& function = drivers_[i].getFunction();
        const auto slot      = function.getDevice();
        if (function.getBus() == 0 && slot >= 8 && slot <= 10)
            disks[slot - 8] = i;
    }
    auto fail = [] {
        os::Sys().log("[M5 LAB] FAIL storage probe\n");
        return 97;
    };
    for (const auto disk : disks)
        if (disk == deviceCapacity) return fail();
    os::Ipc::Handle supervisor(
        os::detail::RegistryClient().lookup("control.device", 5000));
    if (!supervisor.getValid()) return fail();
    // Keep the boot registry alive. Interrupting bootstrap is a different
    // failure mode from replacing an already running storage instance.
    os::Ipc::Handle init(
        os::detail::RegistryClient().lookup("control.init", 15000));
    if (!init.getValid()) return fail();
    os::Task current;
    if (current.openCurrent()) return fail();
    alignas(8) uint8_t block[512]{};
    uint64_t originalOwner = 0;
    if (virtio_[disks[0]].read(8, 1, block, sizeof(block)).getError() !=
        Error::Success)
        return fail();
    os::Runtime::copyBytes(&originalOwner, block, sizeof(originalOwner));
    if (originalOwner) {
        if (originalOwner == current.getId()) return fail();
        for (const auto disk : disks) {
            uint64_t owner = 0;
            if (virtio_[disk].read(8, 1, block, sizeof(block)).getError() !=
                Error::Success)
                return fail();
            os::Runtime::copyBytes(&owner, block, sizeof(owner));
            if (owner != originalOwner) return fail();
        }
        os::Sys().log("[M5 LAB] replacement owner read all three disks\n");
        return 0;
    }
    const auto owner = current.getId();
    os::Runtime::copyBytes(block, &owner, sizeof(owner));
    for (const auto disk : disks)
        if (virtio_[disk].write(8, 1, block, sizeof(block)).getError() !=
                Error::Success ||
            virtio_[disk].flush() != Error::Success)
            return fail();
    if (virtio_[disks[0]].stop() != Stop::Quarantined) return fail();
    os::Device denied;
    if (denied.open(drivers_[disks[0]].getFunction()) == 0) return fail();
    os::Sys().log("[M5 LAB] quarantine rejected claim from live owner\n");
    if (virtio_[disks[1]].stop() != Stop::Unconfirmed) return fail();
    os::Sys().log("[M5 LAB] stop wait returned unconfirmed\n");
    // Leave the third domain active to exercise the real task exit path.
    os::Sys().log("[M5 LAB] original storage task exiting\n");
    return 96;
}
