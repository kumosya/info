    // Test faults are scoped to the two designated disposable PCI disks.
    static bool failedOnce = false, delayedOnce = false;
    if (function_.bus == 0 && function_.device == 8 && !failedOnce) {
        failedOnce = true;
        device::serialWrite("[M5 LAB] injected reset failure\n");
        return false;
    }
    if (function_.bus == 0 && function_.device == 9 && !delayedOnce) {
        delayedOnce = true;
        const uint64_t labStart = time::getMonotonicTime();
        bool released = false, retained = false;
        do {
            {
                ::kernel::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);
                released = references_ == 0;
                if (released) {
                    retained = state_ == State::Stopping && dmaPages_ == 8;
                    for (auto* buffer = dma_; buffer && retained; buffer = buffer->next_) {
                        mm::PhysicalPageInfo frame;
                        retained = mm::queryPhysicalPage(buffer->physicalAddress_, frame) &&
                                   frame.getState() == mm::FrameState::Allocated &&
                                   frame.getOwner() == owner_->id();
                    }
                }
            }
            if (!released) task::yieldThread();
        } while (!released && time::getMonotonicTime() - labStart < 8000000000ULL);
        device::serialWrite(released && retained
            ? "[M5 LAB] pending stop retained physical DMA frames\n"
            : "[M5 LAB] FAIL pending stop frame retention\n");
        // Fall through to the real reset after the calling thread timed out.
    }
