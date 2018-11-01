// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <virtio/remoteproc.h>

// clang-format off

#define VIRTIO_TRUSTY_MAX_NAME_LEGNTH      32u
#define VIRTIO_TRUSTY_NUM_QUEUES           2u

/* Normal memory */
#define NS_MAIR_NORMAL_CACHED_WB_RWA       0xFF /* inner and outer write back read/write allocate */
#define NS_MAIR_NORMAL_CACHED_WT_RA        0xAA /* inner and outer write through read allocate */
#define NS_MAIR_NORMAL_CACHED_WB_RA        0xEE /* inner and outer wriet back, read allocate */
#define NS_MAIR_NORMAL_UNCACHED            0x44 /* uncached */

/* sharaeble attributes */
#define NS_NON_SHAREABLE                   0x0
#define NS_OUTER_SHAREABLE                 0x2
#define NS_INNER_SHAREABLE                 0x3

// clang-format on

__BEGIN_CDECLS;

// Trusty IPC device configuration shared with linux side
typedef struct virtio_trusty_vdev_config {
    uint32_t msg_buf_max_size;                    // max msg size that this device can handle
    uint32_t msg_buf_alignment;                   // required msg alignment (PAGE_SIZE)
    char dev_name[VIRTIO_TRUSTY_MAX_NAME_LEGNTH]; // NS device node name
} __PACKED virtio_trusty_vdev_config_t;

typedef struct virtio_trusty_vdev_descr {
    struct fw_rsc_hdr hdr;
    struct fw_rsc_vdev vdev;
    struct fw_rsc_vdev_vring vrings[VIRTIO_TRUSTY_NUM_QUEUES];
    struct virtio_trusty_vdev_config config;
} __PACKED virtio_trusty_vdev_descr_t;

__END_CDECLS;
