// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <threads.h>
#include <ddk/device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/serial.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <sync/completion.h>
#include <zircon/types.h>

// this struct is local to platform-serial.c
typedef struct platform_serial_port platform_serial_port_t;

// context structure for the platform bus
typedef struct {
    zx_device_t* zxdev;
    usb_mode_switch_protocol_t ums;
    gpio_protocol_t gpio;
    i2c_protocol_t i2c;
    clk_protocol_t clk;
    serial_impl_protocol_t serial;
    iommu_protocol_t iommu;
    zx_handle_t resource;   // root resource for platform bus
    uint32_t vid;
    uint32_t pid;

    list_node_t devices;    // list of platform_dev_t
    char board_name[ZX_DEVICE_NAME_MAX + 1];

    platform_serial_port_t* serial_ports;
    uint32_t serial_port_count;

    // list of i2c_txn_t
    list_node_t i2c_txns;
    mtx_t i2c_txn_lock;

    zx_handle_t dummy_iommu_handle;

    completion_t proto_completion;
} platform_bus_t;

// context structure for a platform device
typedef struct {
    zx_device_t* zxdev;
    platform_bus_t* bus;
    list_node_t node;
    char name[ZX_DEVICE_NAME_MAX + 1];
    uint32_t flags;
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    bool enabled;

    pbus_mmio_t* mmios;
    pbus_irq_t* irqs;
    pbus_gpio_t* gpios;
    pbus_i2c_channel_t* i2c_channels;
    pbus_uart_t* uarts;
    pbus_clk_t* clks;
    pbus_bti_t* btis;
    uint32_t mmio_count;
    uint32_t irq_count;
    uint32_t gpio_count;
    uint32_t i2c_channel_count;
    uint32_t uart_count;
    uint32_t clk_count;
    uint32_t bti_count;
} platform_dev_t;

typedef struct {
    list_node_t node;
    platform_bus_t* bus;
    zx_handle_t channel;
    zx_txid_t txid;
    i2c_complete_cb complete_cb;
    void* cookie;
} i2c_txn_t;


// platform-bus.c
zx_status_t platform_bus_get_protocol(void* ctx, uint32_t proto_id, void* protocol);

// platform-device.c
void platform_dev_free(platform_dev_t* dev);
zx_status_t platform_device_add(platform_bus_t* bus, const pbus_dev_t* dev, uint32_t flags);
zx_status_t platform_device_enable(platform_dev_t* dev, bool enable);

// platform-serial.c
zx_status_t platform_serial_init(platform_bus_t* bus, serial_impl_protocol_t* serial);
void platform_serial_release(platform_bus_t* bus);
zx_status_t platform_serial_config(platform_bus_t* bus, uint32_t port, uint32_t baud_rate,
                                   uint32_t flags);
zx_status_t platform_serial_open_socket(platform_bus_t* bus, uint32_t port, zx_handle_t* out_handle);
