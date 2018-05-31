// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/virtual_interrupt_dispatcher.h>

#include <kernel/auto_lock.h>
#include <dev/interrupt.h>
#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <platform.h>

zx_status_t VirtualInterruptDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher,
                                               zx_rights_t* rights,
                                               uint32_t options) {

    if (options != ZX_INTERRUPT_VIRTUAL)
        return ZX_ERR_INVALID_ARGS;

    // Attempt to construct the dispatcher.
    fbl::AllocChecker ac;
    VirtualInterruptDispatcher* disp = new (&ac) VirtualInterruptDispatcher();
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    // Hold a ref while we check to see if someone else owns this vector or not.
    // If things go wrong, this ref will be released and the IED will get
    // cleaned up automatically.
    auto disp_ref = fbl::AdoptRef<Dispatcher>(disp);

    disp->set_flags(INTERRUPT_VIRTUAL);

    // Transfer control of the new dispatcher to the creator and we are done.
    *rights = ZX_DEFAULT_IRQ_RIGHTS;
    *dispatcher = fbl::move(disp_ref);

    return ZX_OK;
}

//void VirtualInterruptDispatcher::IrqHandler(void* ctx) { }

void VirtualInterruptDispatcher::MaskInterrupt() { }

void VirtualInterruptDispatcher::UnmaskInterrupt() { }

void VirtualInterruptDispatcher::UnregisterInterruptHandler() { }
