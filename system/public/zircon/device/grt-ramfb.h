// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The control device of the RAM framebuffer driver.
#define GRT_RAMFB_CONTROL_DEVICE "/dev/sys/platform/ramfb/mtk_ramfb"

// Return the event the driver signals with ZX_USER_SIGNAL_0 when the guest
// takes over the display.
#define IOCTL_GRT_RAMFB_GET_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_RAMFB, 0)

// Return the event the driver signals with ZX_USER_SIGNAL_0 on each vsync.
#define IOCTL_GRT_RAMFB_GET_VSYNC_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_RAMFB, 1)

// Tell the driver that the guest is taking over the display.
#define IOCTL_GRT_RAMFB_RELEASE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMFB, 2)

// Hide or show the cluster layer; the input is one bool, true to hide.
#define IOCTL_GRT_RAMFB_SET_CLUSTER_HIDE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMFB, 3)

// ssize_t ioctl_grt_ramfb_get_event(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_grt_ramfb_get_event, IOCTL_GRT_RAMFB_GET_EVENT, zx_handle_t);

// ssize_t ioctl_grt_ramfb_get_vsync_event(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_grt_ramfb_get_vsync_event, IOCTL_GRT_RAMFB_GET_VSYNC_EVENT,
                  zx_handle_t);

// ssize_t ioctl_grt_ramfb_release(int fd);
IOCTL_WRAPPER(ioctl_grt_ramfb_release, IOCTL_GRT_RAMFB_RELEASE);

// ssize_t ioctl_grt_ramfb_set_cluster_hide(int fd, const bool* hide);
IOCTL_WRAPPER_IN(ioctl_grt_ramfb_set_cluster_hide, IOCTL_GRT_RAMFB_SET_CLUSTER_HIDE, bool);

// The VMM sends IOCTL_GRT_RAMFB_RELEASE under this name when a guest
// deinitializes its display.
// ssize_t ioctl_display_notify_ramfb(int fd);
IOCTL_WRAPPER(ioctl_display_notify_ramfb, IOCTL_GRT_RAMFB_RELEASE);

__END_CDECLS
