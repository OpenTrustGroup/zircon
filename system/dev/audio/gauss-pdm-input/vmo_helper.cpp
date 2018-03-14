// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddktl/device.h>
#include <zx/vmar.h>

#include "vmo_helper.h"

namespace audio {
namespace gauss {

zx_status_t VmoHelperBase::AllocateVmo(platform_device_protocol_t* pdev,
                                       size_t buffer_size) {
    buffer_size_ = buffer_size;
    zx_status_t status = pdev_alloc_contig_vmo(pdev, buffer_size_, 0, ZX_CACHE_POLICY_CACHED,
                                 ring_buffer_vmo_.reset_and_get_address());

    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to create ring buffer (size %lu, res %d)\n",
               buffer_size_, status);
        ring_buffer_vmo_.reset();
    }

    size_t size = 0;
    status = ring_buffer_vmo_.get_size(&size);

    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to get ring buffer size (expected size %lu, res %d)\n",
               buffer_size_, status);
        ring_buffer_vmo_.reset();
    }

    zxlogf(DEBUG1, "Requested size = 0x%lx\n", buffer_size);
    zxlogf(DEBUG1, "Actual size = 0x%lx\n", size);

    return status;
}

zx_status_t VmoHelper<true>::AllocateVmo(platform_device_protocol_t* pdev,
                                         size_t buffer_size) {
    zx_status_t status = VmoHelperBase::AllocateVmo(pdev, buffer_size);
    if (status != ZX_OK)
        return status;

    return zx_vmar_map(zx_vmar_root_self(), 0u, ring_buffer_vmo_.get(), 0u,
                       buffer_size_,
                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                       &ring_buffer_virt_);
}

zx_status_t VmoHelperBase::GetVmoRange(zx_paddr_t* start_address) {
    return ring_buffer_vmo_.op_range(
        ZX_VMO_OP_LOOKUP, 0, PAGE_SIZE, start_address, sizeof(*start_address));
}

zx_status_t VmoHelperBase::Duplicate(uint32_t rights, zx::vmo* handle) {
    return ring_buffer_vmo_.duplicate(rights, handle);
}

void VmoHelperBase::DestroyVmo() {
    ring_buffer_vmo_.reset();
}

template <bool DEBUG>
zx_status_t VmoHelper<DEBUG>::AllocateVmo(platform_device_protocol_t* pdev,
                                          size_t buffer_size) {
    return VmoHelperBase::AllocateVmo(pdev, buffer_size);
}

template <bool DEBUG>
void VmoHelper<DEBUG>::printoffsetinvmo(uint32_t offset) {}

void VmoHelper<true>::printoffsetinvmo(uint32_t offset) {
    zx_status_t status = ring_buffer_vmo_.op_range(
        ZX_VMO_OP_CACHE_INVALIDATE, 0, buffer_size_, nullptr, 0);

    if (status != ZX_OK) {
        zxlogf(ERROR, "could not cache invalidate\n");
        return;
    }

    zxlogf(DEBUG1, "Current position: 0x%04x. data: ", offset);

    // Print some offsets to probe the data.
    static const uint32_t offsets[] = {0, 0x1000, 0x2000, 0x3000,
                                       0x4000, 0x5000, 0x6000, 0x7000};

    for (const auto& offset : offsets) {
        zxlogf(DEBUG1, " 0x%04x: 0x%08lx,", offset,
               *(reinterpret_cast<uintptr_t*>(offset +
                                              ring_buffer_virt_)));
    }

    // print the last frame of data:
    zxlogf(DEBUG1, "offset is at: 0x%x\n", offset);

    if (offset > 32) {
        uint8_t* frame_start = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(offset) - 32 + ring_buffer_virt_);
        for (int i = 0; i < 32; i++) {
            zxlogf(DEBUG1, "%d: 0x%x, ", i, (frame_start[i] & 0xff));
        }
    }

    zxlogf(DEBUG1, "\n");
}

template <bool DEBUG>
void VmoHelper<DEBUG>::DestroyVmo() {
    return VmoHelperBase::DestroyVmo();
}

void VmoHelper<true>::DestroyVmo() {
    if (ring_buffer_virt_ != 0) {
        zx::vmar::root_self().unmap(
            reinterpret_cast<uintptr_t>(ring_buffer_virt_),
            buffer_size_);
        ring_buffer_virt_ = 0;
    }
}

template class VmoHelper<false>;
}
}
