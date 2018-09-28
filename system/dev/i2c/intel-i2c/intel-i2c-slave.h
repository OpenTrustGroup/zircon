// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <zircon/types.h>
#include <zircon/listnode.h>
#include <stdint.h>

typedef struct intel_serialio_i2c_slave_device {
    zx_device_t* zxdev;
    struct intel_serialio_i2c_device* controller;

    uint8_t chip_address_width;
    uint16_t chip_address;

    struct list_node slave_list_node;
} intel_serialio_i2c_slave_device_t;

// device protocol for a slave device
extern zx_protocol_device_t intel_serialio_i2c_slave_device_proto;

zx_status_t intel_serialio_i2c_slave_transfer(
    intel_serialio_i2c_slave_device_t* slave, i2c_slave_segment_t *segments, int segment_count);
zx_status_t intel_serialio_i2c_slave_get_irq(intel_serialio_i2c_slave_device_t* slave,
                                             zx_handle_t* out);
