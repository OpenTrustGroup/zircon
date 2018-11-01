// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

// Return trusty virtio device client channel
//   in: none
//   out: zx_handle_t
#define IOCTL_TRUSTY_VDEV_START \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_TRUSTY_VDEV, 1)

// Return trusty virtio device max message size
//   in: none
//   out: size_t
#define IOCTL_TRUSTY_VDEV_GET_MESSAGE_SIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TRUSTY_VDEV, 2)

// Return trusty virtio device max message size
//   in: none
//   out: zx_handle_t
#define IOCTL_TRUSTY_VDEV_GET_SHM_RESOURCE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_TRUSTY_VDEV, 3)

// ssize_t ioctl_trusty_vdev_start(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_trusty_vdev_start, IOCTL_TRUSTY_VDEV_START, zx_handle_t);

// ssize_t ioctl_trusty_vdev_get_message_size(int fd, size_t* out);
IOCTL_WRAPPER_OUT(ioctl_trusty_vdev_get_message_size, IOCTL_TRUSTY_VDEV_GET_MESSAGE_SIZE, size_t);

// ssize_t ioctl_trusty_vdev_get_shm_resource(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_trusty_vdev_get_shm_resource, IOCTL_TRUSTY_VDEV_GET_SHM_RESOURCE, zx_handle_t);
