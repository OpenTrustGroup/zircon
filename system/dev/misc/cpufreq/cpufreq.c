// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Publishes /dev/misc/cpufreq for the guest VMM. The emulated machine has no
// CPU frequency scaling, so there are no frequency changes to trace and the
// trace buffer the VMM hands over is accepted and left unused.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/device/cpufreq.h>
#include <zircon/types.h>

#include <stdint.h>

static zx_status_t cpufreq_ioctl(void* ctx, uint32_t op,
                                 const void* cmd, size_t cmdlen,
                                 void* reply, size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_CPUFREQ_TRACE_SET_BUF:
        // The trace buffer's physical address and size.
        if (cmdlen != sizeof(uint64_t) * 2) {
            return ZX_ERR_INVALID_ARGS;
        }
        return ZX_OK;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_protocol_device_t cpufreq_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = cpufreq_ioctl,
};

static zx_status_t cpufreq_bind(void* ctx, zx_device_t* parent) {
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "cpufreq",
        .ops = &cpufreq_device_proto,
    };

    zx_device_t* dev;
    return device_add(parent, &args, &dev);
}

static zx_driver_ops_t cpufreq_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = cpufreq_bind,
};

ZIRCON_DRIVER_BEGIN(cpufreq, cpufreq_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(cpufreq)
