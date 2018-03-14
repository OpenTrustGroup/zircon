// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ring.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <zx/vmar.h>

#include "device.h"
#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

void virtio_dump_desc(const struct vring_desc* desc) {
    printf("vring descriptor %p: ", desc);
    printf("[addr=%#" PRIx64 ", ", desc->addr);
    printf("len=%d, ", desc->len);
    printf("flags=%#04hx, ", desc->flags);
    printf("next=%#04hx]\n", desc->next);
}

Ring::Ring(Device* device)
    : device_(device) {

    memset(&ring_buf_, 0, sizeof(ring_buf_));
}

Ring::~Ring() {
    io_buffer_release(&ring_buf_);
}

zx_status_t Ring::Init(uint16_t index, uint16_t count) {
    LTRACEF("index %u, count %u\n", index, count);

    // XXX check that count is a power of 2

    index_ = index;

    // make sure the count is available in this ring
    uint16_t max_ring_size = device_->GetRingSize(index);
    if (count > max_ring_size) {
        zxlogf(ERROR, "ring init count too big for hardware %u > %u\n", count, max_ring_size);
        return ZX_ERR_OUT_OF_RANGE;
    }

    // allocate a ring
    size_t size = vring_size(count, PAGE_SIZE);
    LTRACEF("need %zu bytes\n", size);

    zx_status_t status = io_buffer_init(&ring_buf_, size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        return status;
    }

    LTRACEF("allocated vring at %p, physical address %#" PRIxPTR "\n",
            io_buffer_virt(&ring_buf_), io_buffer_phys(&ring_buf_));

    /* initialize the ring */
    vring_init(&ring_, count, io_buffer_virt(&ring_buf_), PAGE_SIZE);
    ring_.free_list = 0xffff;
    ring_.free_count = 0;

    /* add all the descriptors to the free list */
    for (uint16_t i = 0; i < count; i++) {
        FreeDesc(i);
    }

    /* register the ring with the device */
    zx_paddr_t pa_desc = io_buffer_phys(&ring_buf_);
    zx_paddr_t pa_avail = pa_desc + ((uintptr_t)ring_.avail - (uintptr_t)ring_.desc);
    zx_paddr_t pa_used = pa_desc + ((uintptr_t)ring_.used - (uintptr_t)ring_.desc);
    device_->SetRing(index_, count, pa_desc, pa_avail, pa_used);

    return ZX_OK;
}

void Ring::FreeDesc(uint16_t desc_index) {
    LTRACEF("index %u free_count %u\n", desc_index, ring_.free_count);
    ring_.desc[desc_index].next = ring_.free_list;
    ring_.free_list = desc_index;
    ring_.free_count++;
}

struct vring_desc* Ring::AllocDescChain(uint16_t count, uint16_t* start_index) {
    if (ring_.free_count < count)
        return NULL;

    /* start popping entries off the chain */
    struct vring_desc* last = 0;
    uint16_t last_index = 0;
    while (count > 0) {
        uint16_t i = ring_.free_list;
        assert(i < ring_.num);

        struct vring_desc* desc = &ring_.desc[i];

        ring_.free_list = desc->next;
        ring_.free_count--;

        if (last) {
            desc->flags |= VRING_DESC_F_NEXT;
            desc->next = last_index;
        } else {
            // first one
            desc->flags &= static_cast<uint16_t>(~VRING_DESC_F_NEXT);
            desc->next = 0;
        }
        last = desc;
        last_index = i;
        count--;
    }

    if (start_index)
        *start_index = last_index;

    return last;
}

void Ring::SubmitChain(uint16_t desc_index) {
    LTRACEF("desc %u\n", desc_index);

    /* add the chain to the available list */
    struct vring_avail* avail = ring_.avail;

    avail->ring[avail->idx & ring_.num_mask] = desc_index;
    //mb();
    avail->idx++;
}

void Ring::Kick() {
    LTRACE_ENTRY;

    device_->RingKick(index_);
}

} // namespace virtio
