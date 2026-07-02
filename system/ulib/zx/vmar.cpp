// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmar.h>

#include <zircon/syscalls.h>
#include <unistd.h>

namespace zx {

zx_status_t vmar::allocate(size_t offset, size_t size, uint32_t flags,
                           vmar* child, uintptr_t* child_addr) const {
    zx_handle_t h;
    zx_status_t status = zx_vmar_allocate(get(), offset, size, flags, &h, child_addr);
    if (status == ZX_OK) {
        child->reset(h);
    } else {
        child->reset(ZX_HANDLE_INVALID);
    }
    return status;
}

zx_status_t vmar::op_range(uint32_t op, uintptr_t address, size_t size, void* buffer,
                           size_t buffer_size) const {
    zx_status_t status = zx_vmar_op_range(get(), op, address, size, buffer, buffer_size);
    if (status != ZX_OK) {
        return status;
    }

    const size_t page_size = getpagesize();
    if (op == ZX_VMAR_OP_COMMIT) {
        for (uintptr_t addr = address; addr < address + size; addr += page_size) {
            *reinterpret_cast<volatile uint8_t*>(addr) = *reinterpret_cast<volatile uint8_t*>(addr);
        }
    } else if (op == ZX_VMAR_OP_ALWAYS_NEED) {
        for (uintptr_t addr = address; addr < address + size; addr += page_size) {
            (void)*reinterpret_cast<volatile const uint8_t*>(addr);
        }
    }
    return ZX_OK;
}

} // namespace zx
