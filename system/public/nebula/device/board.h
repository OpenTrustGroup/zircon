// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The request of IOCTL_NBL_BOARD_PDEV_ENABLE: the platform device to enable or
// disable, named by its platform bus vendor, product and device IDs.
typedef struct {
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    bool enable;
} pdev_enable_info_t;

// Adds a platform device the board driver registered disabled to the device
// tree, or removes it again. Sent to the board driver's "nbl_board" device.
//   in: pdev_enable_info_t
//   out: none
#define IOCTL_NBL_BOARD_PDEV_ENABLE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NEBULA, 0x10)

// ssize_t ioctl_nbl_board_pdev_enable(int fd, const pdev_enable_info_t* in);
IOCTL_WRAPPER_IN(ioctl_nbl_board_pdev_enable, IOCTL_NBL_BOARD_PDEV_ENABLE,
                 pdev_enable_info_t);

__END_CDECLS
