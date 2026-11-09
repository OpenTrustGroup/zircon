// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The control device of the platform watchdog driver.
#define GRT_WDT_CONTROL_DEVICE "/dev/sys/platform/wdt/mtk_wdt"

// Start and stop the hardware watchdog. The driver refuses to stop it while
// the SOS or the guest manager is supervised by it.
#define IOCTL_GRT_WDT_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 0)
#define IOCTL_GRT_WDT_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 1)

// Report that the SOS is alive. IOCTL_GRT_WDT_ACTIVE is a synonym.
#define IOCTL_GRT_WDT_PING \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 2)
#define IOCTL_GRT_WDT_ACTIVE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 3)

// Set the SOS heartbeat timeout in seconds; the input is one uint32_t.
#define IOCTL_GRT_WDT_SET_TIMEOUT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 4)

// Ask the watchdog to reset the platform.
#define IOCTL_GRT_WDT_TRIGGER_RESET \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 5)

// Record that the next reset is a software watchdog reset.
#define IOCTL_GRT_SET_RST_WDT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 6)

// Suspend and resume watchdog supervision across a platform suspend.
#define IOCTL_GRT_SET_SUSPEND_WDT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 7)
#define IOCTL_GRT_SET_RESUME_WDT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 8)

// Configure supervision of the guest manager; the input is a
// grt_gm_watchdog_config_t.
#define IOCTL_GRT_SET_GM_WATCHDOG_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 9)

// Report that the guest manager is alive.
#define IOCTL_GRT_SET_GM_HEARTBEAT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WDT, 10)

// Guest manager supervision settings. A zero interval, threshold or grace
// period leaves the driver's default in place.
typedef struct grt_gm_watchdog_config {
    // Seconds between two guest manager heartbeats.
    uint32_t heartbeat_interval_sec;
    // Consecutive missed heartbeats that cause a platform reset.
    uint32_t miss_threshold;
    // Seconds after enabling during which missed heartbeats are not counted.
    uint32_t boot_grace_sec;
    // Nonzero to enable supervision, zero to disable it.
    uint8_t enable;
    uint8_t reserved[3];
} grt_gm_watchdog_config_t;

// ssize_t ioctl_grt_wdt_start(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_start, IOCTL_GRT_WDT_START);

// ssize_t ioctl_grt_wdt_stop(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_stop, IOCTL_GRT_WDT_STOP);

// ssize_t ioctl_grt_wdt_ping(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_ping, IOCTL_GRT_WDT_PING);

// ssize_t ioctl_grt_wdt_active(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_active, IOCTL_GRT_WDT_ACTIVE);

// ssize_t ioctl_grt_wdt_set_timeout(int fd, const uint32_t* timeout_sec);
IOCTL_WRAPPER_IN(ioctl_grt_wdt_set_timeout, IOCTL_GRT_WDT_SET_TIMEOUT, uint32_t);

// ssize_t ioctl_grt_wdt_trigger_reset(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_trigger_reset, IOCTL_GRT_WDT_TRIGGER_RESET);

// ssize_t ioctl_grt_wdt_set_rst_status(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_set_rst_status, IOCTL_GRT_SET_RST_WDT);

// ssize_t ioctl_grt_wdt_set_suspend(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_set_suspend, IOCTL_GRT_SET_SUSPEND_WDT);

// ssize_t ioctl_grt_wdt_set_resume(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_set_resume, IOCTL_GRT_SET_RESUME_WDT);

// ssize_t ioctl_grt_wdt_set_gm_watchdog_config(int fd,
//                                              const grt_gm_watchdog_config_t* in);
IOCTL_WRAPPER_IN(ioctl_grt_wdt_set_gm_watchdog_config, IOCTL_GRT_SET_GM_WATCHDOG_CONFIG,
                 grt_gm_watchdog_config_t);

// ssize_t ioctl_grt_wdt_set_gm_heartbeat(int fd);
IOCTL_WRAPPER(ioctl_grt_wdt_set_gm_heartbeat, IOCTL_GRT_SET_GM_HEARTBEAT);

__END_CDECLS
