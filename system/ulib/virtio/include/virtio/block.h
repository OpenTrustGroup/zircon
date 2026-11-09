// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>

// clang-format off
#define VIRTIO_BLK_F_BARRIER    (1u << 0)
#define VIRTIO_BLK_F_SIZE_MAX   (1u << 1)
#define VIRTIO_BLK_F_SEG_MAX    (1u << 2)
#define VIRTIO_BLK_F_GEOMETRY   (1u << 4)
#define VIRTIO_BLK_F_RO         (1u << 5)
#define VIRTIO_BLK_F_BLK_SIZE   (1u << 6)
#define VIRTIO_BLK_F_SCSI       (1u << 7)
#define VIRTIO_BLK_F_FLUSH      (1u << 9)
#define VIRTIO_BLK_F_TOPOLOGY   (1u << 10)
#define VIRTIO_BLK_F_CONFIG_WCE (1u << 11)

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1
#define VIRTIO_BLK_T_FLUSH      4

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2
// clang-format on

__BEGIN_CDECLS

typedef struct virtio_blk_geometry {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
} __PACKED virtio_blk_geometry_t;

typedef struct virtio_blk_config {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    virtio_blk_geometry_t geometry;
    uint32_t blk_size;
    // The fields below are the later ones of VIRTIO 1.2, Section 5.2.4, laid
    // out as Linux's include/uapi/linux/virtio_blk.h has them.
    uint8_t physical_block_exp;
    uint8_t alignment_offset;
    uint16_t min_io_size;
    uint32_t opt_io_size;
    uint8_t wce;
    uint8_t unused;
    uint16_t num_queues;
    uint32_t max_discard_sectors;
    uint32_t max_discard_seg;
    uint32_t discard_sector_alignment;
    uint32_t max_write_zeroes_sectors;
    uint32_t max_write_zeroes_seg;
    uint8_t write_zeroes_may_unmap;
    uint8_t unused1[3];
    uint32_t max_secure_erase_sectors;
    uint32_t max_secure_erase_seg;
    uint32_t secure_erase_sector_alignment;
    struct {
        uint32_t zone_sectors;
        uint32_t max_open_zones;
        uint32_t max_active_zones;
        uint32_t max_append_sectors;
        uint32_t write_granularity;
        uint8_t model;
        uint8_t unused2[3];
    } __PACKED zoned;
    // Nebula's own, past what the specification defines: the UFS logical
    // unit behind the device and its inline encryption capabilities.
    uint32_t ufs_lun;
    struct blk_crypto_cap crypto_cap;
} __PACKED virtio_blk_config_t;

typedef struct virtio_blk_req {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} __PACKED virtio_blk_req_t;

__END_CDECLS
