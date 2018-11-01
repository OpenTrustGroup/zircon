// Copyright 2018
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <virtio/virtio.h>

extern zx_status_t virtio_trusty_bind(void* ctx, zx_device_t* device);

static zx_driver_ops_t virtio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = virtio_trusty_bind,
};

ZIRCON_DRIVER_BEGIN(trusty_virtio, virtio_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(virtio)
