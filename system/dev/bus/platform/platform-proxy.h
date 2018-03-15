// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/usb-mode-switch.h>

// maximum transfer size we can proxy.
#define PDEV_I2C_MAX_TRANSFER_SIZE 4096

// RPC ops
enum {
    // ZX_PROTOCOL_PLATFORM_DEV
    PDEV_GET_MMIO = 1,
    PDEV_GET_INTERRUPT,
    PDEV_GET_BTI,
    PDEV_ALLOC_CONTIG_VMO,
    PDEV_GET_DEVICE_INFO,

    // ZX_PROTOCOL_USB_MODE_SWITCH
    PDEV_UMS_GET_INITIAL_MODE,
    PDEV_UMS_SET_MODE,

    // ZX_PROTOCOL_GPIO
    PDEV_GPIO_CONFIG,
    PDEV_GPIO_SET_ALT_FUNCTION,
    PDEV_GPIO_READ,
    PDEV_GPIO_WRITE,

    // ZX_PROTOCOL_I2C
    PDEV_I2C_GET_CHANNEL,
    PDEV_I2C_TRANSACT,
    PDEV_I2C_SET_BITRATE,
    PDEV_I2C_CHANNEL_RELEASE,

    // ZX_PROTOCOL_SERIAL
    PDEV_SERIAL_CONFIG,
    PDEV_SERIAL_OPEN_SOCKET,

    // ZX_PROTOCOL_CLK
    PDEV_CLK_ENABLE,
    PDEV_CLK_DISABLE,
};

// context for i2c_transact
typedef struct {
    size_t write_length;
    size_t read_length;
    i2c_complete_cb complete_cb;
    void* cookie;
} pdev_i2c_txn_ctx_t;

typedef struct {
    size_t size;
    uint32_t align_log2;
    uint32_t cache_policy;
} pdev_config_vmo_t;

typedef struct {
    pdev_i2c_txn_ctx_t txn_ctx;
    // private context for the server, returned from i2c_get_channel and passed by client
    // for all channel operations
    void* server_ctx;
    uint32_t bitrate;
} pdev_i2c_req_t;

typedef struct {
    pdev_i2c_txn_ctx_t txn_ctx;
    // private context for the server, returned from i2c_get_channel and passed by client
    // for all channel operations
    void* server_ctx;
    size_t max_transfer_size;
} pdev_i2c_resp_t;

typedef struct {
    zx_txid_t txid;
    uint32_t op;
    uint32_t index;
    union {
        pdev_config_vmo_t contig_vmo;
        usb_mode_t usb_mode;
        uint32_t gpio_flags;
        uint32_t gpio_alt_function;
        uint8_t gpio_value;
        pdev_i2c_req_t i2c;
        struct {
            uint32_t baud_rate;
            uint32_t flags;
        } serial_config;
    };
} pdev_req_t;

typedef struct {
    zx_txid_t txid;
    zx_status_t status;
    union {
        usb_mode_t usb_mode;
        uint8_t gpio_value;
        pdev_i2c_resp_t i2c;
        struct {
            zx_off_t offset;
            size_t length;
        } mmio;
        pdev_device_info_t info;
    };
} pdev_resp_t;
