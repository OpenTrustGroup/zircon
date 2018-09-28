// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/mmio-buffer.h>

#include <string.h>

#include <ddk/driver.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

zx_status_t mmio_buffer_init(mmio_buffer_t* buffer, zx_off_t offset, size_t size,
                             zx_handle_t vmo, uint32_t cache_policy) {
    zx_status_t status = zx_vmo_set_cache_policy(vmo, cache_policy);
    if (status != ZX_OK) {
        zx_handle_close(vmo);
        return status;
    }

    uintptr_t vaddr;
    const size_t vmo_offset = ROUNDDOWN(offset, ZX_PAGE_SIZE);
    const size_t page_offset = offset - vmo_offset;
    const size_t vmo_size = ROUNDUP(size + page_offset, ZX_PAGE_SIZE);

    status = zx_vmar_map(zx_vmar_root_self(),
                         ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE,
                         0, vmo, vmo_offset, vmo_size, &vaddr);
    if (status != ZX_OK) {
        zx_handle_close(vmo);
        return status;
    }

    buffer->vmo = vmo;
    buffer->vaddr = (void*)(vaddr + page_offset);
    buffer->offset = offset;
    buffer->size = size;

    return ZX_OK;
}

void mmio_buffer_release(mmio_buffer_t* buffer) {
    if (buffer->vmo != ZX_HANDLE_INVALID) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)buffer->vaddr, buffer->size);
        zx_handle_close(buffer->vmo);
        buffer->vmo = ZX_HANDLE_INVALID;
    }
}

zx_status_t mmio_buffer_pin(mmio_buffer_t* buffer, zx_handle_t bti, mmio_pinned_buffer_t* out) {
    zx_paddr_t paddr;
    zx_handle_t pmt;
    const uint32_t options = ZX_BTI_PERM_WRITE | ZX_BTI_PERM_READ;
    const size_t vmo_offset = ROUNDDOWN(buffer->offset, ZX_PAGE_SIZE);
    const size_t page_offset = buffer->offset - vmo_offset;
    const size_t vmo_size = ROUNDUP(buffer->size + page_offset, ZX_PAGE_SIZE);

    zx_status_t status = zx_bti_pin(bti, options, buffer->vmo, vmo_offset, vmo_size,
                                    &paddr, 1, &pmt);
    if (status != ZX_OK) {
        return status;
    }

    out->mmio = buffer;
    out->paddr = paddr + page_offset;
    out->pmt = pmt;

    return ZX_OK;
}

void mmio_buffer_unpin(mmio_pinned_buffer_t* buffer) {
    if (buffer->pmt != ZX_HANDLE_INVALID) {
        zx_pmt_unpin(buffer->pmt);
        buffer->pmt = ZX_HANDLE_INVALID;
    }
}
