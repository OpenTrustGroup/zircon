// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <object/vqueue_notifier_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zircon/rights.h>

namespace {

fbl::Mutex notifiers_lock;
fbl::DoublyLinkedList<VqueueNotifierDispatcher*> notifiers TA_GUARDED(notifiers_lock);

} // namespace

zx_status_t VqueueNotifierDispatcher::Create(uint32_t kick_irq, int32_t vmid,
                                             fbl::RefPtr<Dispatcher>* dispatcher,
                                             zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) VqueueNotifierDispatcher(vmid);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    {
        fbl::AutoLock lock(&notifiers_lock);
        notifiers.push_back(disp);
        disp->registered_ = true;
    }

    *rights = ZX_RIGHTS_BASIC | ZX_RIGHT_SIGNAL;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

void VqueueNotifierDispatcher::Notify(int32_t vmid) {
    fbl::AutoLock lock(&notifiers_lock);
    for (auto& notifier : notifiers) {
        if (notifier.vmid_ == vmid) {
            notifier.UpdateState(0u, ZX_USER_SIGNAL_0);
        }
    }
}

VqueueNotifierDispatcher::VqueueNotifierDispatcher(int32_t vmid)
    : vmid_(vmid) {}

VqueueNotifierDispatcher::~VqueueNotifierDispatcher() {
    on_zero_handles();
}

void VqueueNotifierDispatcher::on_zero_handles() {
    fbl::AutoLock lock(&notifiers_lock);
    if (registered_) {
        notifiers.erase(*this);
        registered_ = false;
    }
}
