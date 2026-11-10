// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>

namespace zx {

class interrupt : public object<interrupt> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_INTERRUPT_SLOT;

    constexpr interrupt() = default;

    explicit interrupt(zx_handle_t value) : object(value) {}

    explicit interrupt(handle&& h) : object(h.release()) {}

    interrupt(interrupt&& other) : object(other.release()) {}

    interrupt& operator=(interrupt&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(const resource& resource, uint32_t options, interrupt* result);

    zx_status_t bind(uint32_t slot, const resource& resource, uint32_t vector, uint32_t options) {
        return zx_interrupt_slot_bind(get(), slot, resource.get(), vector, options);
    }

    // Binds a physical interrupt for the guest of user VM ID |user_vmid|.
    zx_status_t bind(uint32_t slot, const resource& resource, uint32_t vector, uint32_t options,
                     int32_t user_vmid) {
        return zx_interrupt_bind_vm(get(), slot, resource.get(), vector, options, user_vmid);
    }

    zx_status_t bind_vcpu(zx_handle_t vcpu, bool exclusive) {
        return zx_interrupt_bind_vcpu(get(), vcpu, exclusive);
    }

    zx_status_t wait(uint64_t* slots) {
        return zx_interrupt_slot_wait(get(), slots);
    }

    zx_status_t configure_read(uint32_t reg, uint64_t* val) {
        return zx_interrupt_configure_read(get(), reg, val);
    }

    zx_status_t configure_write(uint32_t reg, uint64_t val) {
        return zx_interrupt_configure_write(get(), reg, val);
    }

    // Masking, unmasking, setting pending and configuring the trigger mode of
    // a passed-through interrupt are not supported: such an interrupt is not
    // delivered to the guest.
    zx_status_t mask_interrupt(uint32_t vector) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t unmask_interrupt(uint32_t vector) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t set_pending(bool pending) { return ZX_ERR_NOT_SUPPORTED; }
    zx_status_t configure(uint8_t mode) { return ZX_ERR_NOT_SUPPORTED; }

    zx_status_t get_timestamp(uint32_t slot, zx::time* timestamp) {
        return zx_interrupt_get_timestamp(get(), slot, timestamp->get_address());
    }

    zx_status_t signal(uint32_t slot, zx::time timestamp) {
        return zx_interrupt_signal(get(), slot, timestamp.get());
    }
};

using unowned_interrupt = const unowned<interrupt>;

} // namespace zx
