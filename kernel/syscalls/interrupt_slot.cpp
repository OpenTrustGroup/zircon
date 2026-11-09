// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// The slot-based interrupt calls, as upstream had them at d3b505451, beside
// this kernel's own interrupt calls. This kernel's zx_interrupt_create, _bind
// and _wait are the later one-interrupt-per-handle calls, so upstream's go by
// zx_interrupt_slot_create, _bind and _wait here; zx_interrupt_get_timestamp
// and zx_interrupt_signal keep their names. The calls a VMM uses to pass
// interrupts through to a guest are added on top.

#include <err.h>
#include <trace.h>

#include <dev/interrupt.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/resources.h>
#include <object/slot_interrupt_event_dispatcher.h>
#include <object/vcpu_dispatcher.h>

#include <fbl/ref_ptr.h>
#include <zircon/types.h>

#include "priv.h"

#define LOCAL_TRACE 0

zx_status_t sys_interrupt_slot_create(zx_handle_t hrsrc, uint32_t options,
                                      user_out_handle* out_handle) {
    LTRACEF("options 0x%x\n", options);

    if (options != 0u)
        return ZX_ERR_INVALID_ARGS;

    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    zx_status_t result = SlotInterruptEventDispatcher::Create(&dispatcher, &rights);
    if (result != ZX_OK)
        return result;

    return out_handle->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_interrupt_slot_bind(zx_handle_t handle, uint32_t slot, zx_handle_t hrsrc,
                                    uint32_t vector, uint32_t options) {
    LTRACEF("handle %x\n", handle);

    // resource not required for virtual interrupts
    if (!(options & ZX_INTERRUPT_VIRTUAL)) {
        // TODO(ZX-971): finer grained validation
        zx_status_t status;
        if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
            return status;
        }
    }

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<SlotInterruptDispatcher> interrupt;
    zx_status_t status = up->GetDispatcher(handle, &interrupt);
    if (status != ZX_OK)
        return status;

    return interrupt->Bind(slot, vector, options);
}

zx_status_t sys_interrupt_slot_wait(zx_handle_t handle, user_out_ptr<uint64_t> out_slots) {
    LTRACEF("handle %x\n", handle);

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<SlotInterruptDispatcher> interrupt;
    zx_status_t status = up->GetDispatcher(handle, &interrupt);
    if (status != ZX_OK)
        return status;

    uint64_t slots = 0;
    status = interrupt->WaitForInterrupt(&slots);
    if (status == ZX_OK)
        status = out_slots.copy_to_user(slots);
    return status;
}

zx_status_t sys_interrupt_get_timestamp(zx_handle_t handle, uint32_t slot,
                                        user_out_ptr<zx_time_t> out_timestamp) {
    LTRACEF("handle %x\n", handle);

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<SlotInterruptDispatcher> interrupt;
    zx_status_t status = up->GetDispatcher(handle, &interrupt);
    if (status != ZX_OK)
        return status;

    zx_time_t timestamp;
    status = interrupt->GetTimeStamp(slot, &timestamp);
    if (status == ZX_OK)
        status = out_timestamp.copy_to_user(timestamp);
    return status;
}

zx_status_t sys_interrupt_signal(zx_handle_t handle, uint32_t slot, zx_time_t timestamp) {
    LTRACEF("handle %x\n", handle);

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<SlotInterruptDispatcher> interrupt;
    zx_status_t status = up->GetDispatcher(handle, &interrupt);
    if (status != ZX_OK)
        return status;

    return interrupt->UserSignal(slot, timestamp);
}

// Binds a physical interrupt for a guest. Delivering a passed-through
// interrupt to the guest is not implemented: one bound with
// ZX_INTERRUPT_PASSTHROUGH is accepted and stays masked in the host. Without
// that option this is zx_interrupt_slot_bind.
// TODO(sychiu): Deliver passed-through interrupts to the guest's vCPUs.
zx_status_t sys_interrupt_bind_vm(zx_handle_t handle, uint32_t slot, zx_handle_t hrsrc,
                                  uint32_t vector, uint32_t options, int32_t user_vmid) {
    if (!(options & ZX_INTERRUPT_PASSTHROUGH)) {
        return sys_interrupt_slot_bind(handle, slot, hrsrc, vector, options);
    }

    zx_status_t status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT);
    if (status != ZX_OK)
        return status;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<SlotInterruptDispatcher> interrupt;
    return up->GetDispatcher(handle, &interrupt);
}

// Routes a passed-through interrupt to one vCPU; with passed-through
// interrupts not delivered, this only checks its handles.
zx_status_t sys_interrupt_bind_vcpu(zx_handle_t handle, zx_handle_t vcpu, bool exclusive) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<SlotInterruptDispatcher> interrupt;
    zx_status_t status = up->GetDispatcher(handle, &interrupt);
    if (status != ZX_OK)
        return status;
    fbl::RefPtr<VcpuDispatcher> vcpu_dispatcher;
    return up->GetDispatcher(vcpu, &vcpu_dispatcher);
}

// A VMM that passes interrupts through to a guest reads the distributor's
// global and configuration registers, and clears pending and active state, on
// the guest's behalf.
zx_status_t sys_interrupt_configure_read(zx_handle_t handle, uint32_t reg,
                                         user_out_ptr<uint64_t> out_val) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<SlotInterruptDispatcher> interrupt;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &interrupt);
    if (status != ZX_OK)
        return status;

    uint64_t val;
    status = read_interrupt_distributor(reg, &val);
    if (status != ZX_OK)
        return status;
    return out_val.copy_to_user(val);
}

zx_status_t sys_interrupt_configure_write(zx_handle_t handle, uint32_t reg, uint64_t val) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<SlotInterruptDispatcher> interrupt;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &interrupt);
    if (status != ZX_OK)
        return status;

    return write_interrupt_distributor(reg, val);
}
