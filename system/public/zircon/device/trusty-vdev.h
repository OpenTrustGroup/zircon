// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

// Return trusty virtio device client channel
//   in: none
//   out: zx_handle_t
#define IOCTL_TRUSTY_VDEV_START \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_TRUSTY_VDEV, 0)

IOCTL_WRAPPER_OUT(ioctl_trusty_vdev_start, IOCTL_TRUSTY_VDEV_START, zx_handle_t);
