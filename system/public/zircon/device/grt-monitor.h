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

// The control device of the heartbeat monitor driver.
#define GRT_MONITOR_CONTROL_DEVICE "/dev/sys/platform/monitor/mtk_monitor"

// Pass a guest heartbeat buffer to the monitor; the input is a
// grt_monitor_ioctl_data_t.
#define IOCTL_GRT_MONITOR_SEND_DATA \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_MONITOR, 0)

// Return the monitor's event. The monitor signals ZX_USER_SIGNAL_0 when the
// heartbeat times out and ZX_USER_SIGNAL_1 when it resumes.
#define IOCTL_GRT_GET_MONITOR_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_MONITOR, 1)

// Enable or disable the heartbeat timer; the input is one bool.
#define IOCTL_GRT_ENABLE_MONITOR \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_MONITOR, 2)

typedef struct grt_monitor_ioctl_data {
    // Physical address of the heartbeat data, as the guest passed it.
    uint64_t phy_addr;
    // Size of the heartbeat data in bytes.
    uint64_t size;
} grt_monitor_ioctl_data_t;

// ssize_t ioctl_grt_monitor_send_data(int fd, const grt_monitor_ioctl_data_t* in);
IOCTL_WRAPPER_IN(ioctl_grt_monitor_send_data, IOCTL_GRT_MONITOR_SEND_DATA,
                 grt_monitor_ioctl_data_t);

// ssize_t ioctl_grt_get_monitor_event(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_grt_get_monitor_event, IOCTL_GRT_GET_MONITOR_EVENT, zx_handle_t);

// ssize_t ioctl_grt_enable_monitor(int fd, const bool* enable);
IOCTL_WRAPPER_IN(ioctl_grt_enable_monitor, IOCTL_GRT_ENABLE_MONITOR, bool);

__END_CDECLS
