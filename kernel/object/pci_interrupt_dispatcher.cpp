// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE

#include <object/pci_interrupt_dispatcher.h>

#include <kernel/auto_lock.h>
#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <object/pci_device_dispatcher.h>
#include <platform.h>

PciInterruptDispatcher::~PciInterruptDispatcher() {
    // Release our reference to our device.
    device_ = nullptr;
}

pcie_irq_handler_retval_t PciInterruptDispatcher::IrqThunk(const PcieDevice& dev,
                                                           uint irq_id,
                                                           void* ctx) {
    DEBUG_ASSERT(ctx);
    PciInterruptDispatcher* thiz
            = reinterpret_cast<PciInterruptDispatcher *>(ctx);
    thiz->InterruptHandler(true);
    return PCIE_IRQRET_MASK;
}

zx_status_t PciInterruptDispatcher::Create(
        const fbl::RefPtr<PcieDevice>& device,
        uint32_t irq_id,
        bool maskable,
        zx_rights_t* out_rights,
        fbl::RefPtr<Dispatcher>* out_interrupt) {
    // Sanity check our args
    if (!device || !out_rights || !out_interrupt) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!is_valid_interrupt(irq_id, 0)) {
        return ZX_ERR_INTERNAL;
    }

    fbl::AllocChecker ac;
    // Attempt to allocate a new dispatcher wrapper.
    auto interrupt_dispatcher = new (&ac) PciInterruptDispatcher(device, maskable);
    fbl::RefPtr<Dispatcher> dispatcher = fbl::AdoptRef<Dispatcher>(interrupt_dispatcher);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    fbl::AutoLock lock(interrupt_dispatcher->get_lock());

    // Register the interrupt
    zx_status_t status = interrupt_dispatcher->RegisterInterruptHandler_HelperLocked(irq_id,
                                                                   INTERRUPT_UNMASK_PREWAIT);
    if (status != ZX_OK)
        return status;

    // Everything seems to have gone well.  Make sure the interrupt is unmasked
    // (if it is maskable) then transfer our dispatcher refererence to the
    // caller.
    if (maskable) {
        device->UnmaskIrq(irq_id);
    }
    *out_interrupt = fbl::move(dispatcher);
    *out_rights    = ZX_DEFAULT_PCI_INTERRUPT_RIGHTS;
    return ZX_OK;
}

void PciInterruptDispatcher::MaskInterrupt(uint32_t vector) {
    if (maskable_)
        device_->MaskIrq(vector);
}

void PciInterruptDispatcher::UnmaskInterrupt(uint32_t vector) {
    if (maskable_)
        device_->UnmaskIrq(vector);
}

zx_status_t PciInterruptDispatcher::RegisterInterruptHandler(uint32_t vector, void* data) {
    return device_->RegisterIrqHandler(vector, IrqThunk, data);
}

void PciInterruptDispatcher::UnregisterInterruptHandler(uint32_t vector) {
    device_->RegisterIrqHandler(vector, nullptr, nullptr);
}

#endif  // if WITH_DEV_PCIE
