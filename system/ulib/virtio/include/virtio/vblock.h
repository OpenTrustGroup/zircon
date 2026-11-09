// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Virtio block extensions the Nebula VMM and its block back end use, beside
// <virtio/block.h>.

#include <virtio/block.h>
#include <zircon/device/vblock.h>

// Standard virtio block features and request types (VIRTIO 1.1, Section 5.2;
// the values Linux's include/uapi/linux/virtio_blk.h has).
#define VIRTIO_BLK_F_DISCARD        (1u << 13)
#define VIRTIO_BLK_F_WRITE_ZEROES   (1u << 14)

#define VIRTIO_BLK_T_DISCARD        11
#define VIRTIO_BLK_T_WRITE_ZEROES   13
#define VIRTIO_BLK_T_SECURE_ERASE   14

// Nebula's own: inline encryption, UFS logical units and a pass-through
// ioctl request. Feature bits 22 and 23 and type 0x100 are outside what the
// virtio specification and Linux assign.
#define VIRTIO_BLK_F_INLINE_CRYPTO  (1u << 22)
#define VIRTIO_BLK_F_UFS_LUN        (1u << 23)

#define VIRTIO_BLK_T_IOCTL          0x100

// A request header without any extension.
typedef virtio_blk_req_t virtio_blk_base_req_t;
