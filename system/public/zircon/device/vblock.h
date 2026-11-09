// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// The virtual block back end: a guest's virtio block requests reach the block
// driver of the host through a pair of FIFOs, and the driver reads and writes
// the guest's memory directly through a VMO covering it.

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/device/blk_crypto.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The vblock ioctls share the block family.  <zircon/device/block.h> uses
// numbers 1 to 19 and <zircon/device/pvblk.h> 0x90 to 0x96, so these take
// 0x80 to 0x8c.

// Start serving a guest.  in: struct start_param.
#define IOCTL_VBLOCK_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x80)
// Stop serving a guest.  in: the backend vmid, an int.
#define IOCTL_VBLOCK_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x81)
// Select the guest that later ioctls apply to.  in: the backend vmid, an int.
#define IOCTL_VBLOCK_SET_GUEST \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x82)
// Get the number of blocks the selected guest may see.  out: a uint64_t, zero
// for the whole device.
#define IOCTL_VBLOCK_GET_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x83)
// Hand over a VMO covering the selected guest's memory.  in: the VMO handle,
// which the driver takes.
#define IOCTL_VBLOCK_SET_GPA_RANGE \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_BLOCK, 0x84)
// Get the peer end of a request FIFO.  in: get_handle_param_t; out: the FIFO
// handle.  fifo_id 0 is the submission FIFO, of struct fifo_in_item, and 1
// the completion FIFO, of struct fifo_out_item.
#define IOCTL_VBLOCK_GET_FIFO_HANDLES \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_BLOCK, 0x85)
// Start synchronous direct reads for a guest.  in: struct start_param.
#define IOCTL_VBLOCK_DIRECT_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x86)
// Hand over a VMO covering the direct-read guest's memory.  in: the VMO
// handle, which the driver takes.
#define IOCTL_VBLOCK_DIRECT_SET_GPA_RANGE \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_BLOCK, 0x87)
// Read from the device into guest memory.  in: an int32_t vmid followed by a
// struct direct_io_rw_param.
#define IOCTL_VBLOCK_DIRECT_READ \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x88)
// Select the boot logical unit of a UFS device.  in: the unit, a uint32_t.
#define IOCTL_VBLOCK_SET_BOOT_REGION \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x89)
// Change the write protection of the device.
#define IOCTL_VBLOCK_SET_WRITE_PROTECT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x8a)
// Get the kind of boot device.
#define IOCTL_VBLOCK_GET_BOOTDEV_TYPE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x8b)
// Get the active boot partition of a UFS device.  out: a uint32_t.
#define IOCTL_VBLOCK_GET_ACTIVE_BOOT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 0x8c)

// The depth of each request FIFO, which matches the virtio queue size.  A
// FIFO holds at most one page, so struct fifo_in_item may not grow past 16
// bytes.
#define FIFO_QDEPTH 256

// A range of guest physical addresses.  |end| is inclusive.
struct mem_region {
    uint64_t start;
    uint64_t end;
};

static inline uint64_t mem_region_size(const struct mem_region* mem) {
    return mem->end - mem->start + 1;
}

// The argument of IOCTL_VBLOCK_START and IOCTL_VBLOCK_DIRECT_START.
struct start_param {
    uint16_t vmid;
    struct mem_region mem;
};

// The argument of IOCTL_VBLOCK_GET_FIFO_HANDLES.
typedef struct {
    uint16_t vmid;
    uint16_t fifo_id;
} get_handle_param_t;

// One synchronous read into guest memory.
struct direct_io_rw_param {
    // The guest physical address to read into.
    uint64_t addr;
    // The device offset in bytes.
    uint64_t offset;
    // The length in bytes.
    uint32_t len;
};

// A request the guest submits: the guest physical address of its virtio
// descriptor chain, the number of descriptors and the chain's head index.
struct fifo_in_item {
    uint64_t addr;
    uint32_t len;
    uint32_t head;
};

// A completed request, by the head index it was submitted with.
struct fifo_out_item {
    uint32_t head;
};

// The header of a virtio block request.  The first three fields are the
// standard struct virtio_blk_outhdr; the rest carries the inline encryption
// extension and is meaningful only when use_crypto is set.
typedef struct {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
    // Nonzero if the request is encrypted.
    uint8_t use_crypto;
    // The guest's keyslot for the key.
    uint8_t key_index;
    // Bit 0 toggles each time the guest replaces the key in key_index, which
    // tells the host to program |key| again.
    uint8_t bmap;
    uint8_t reserved[5];
    // The data unit number of the first data unit.
    uint64_t bc_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
    struct blk_crypto_base_key key;
} req_hdr_t;

__END_CDECLS
