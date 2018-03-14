// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    // physical address of MMIO region
    // does not need to be page aligned
    zx_paddr_t  base;
    // length of MMIO region in bytes
    // does not need to be page aligned
    size_t      length;
} pbus_mmio_t;

typedef struct {
    uint32_t    irq;
    uint32_t    mode;   // ZX_INTERRUPT_MODE_* flags
} pbus_irq_t;

typedef struct {
    uint32_t    gpio;
} pbus_gpio_t;

typedef struct {
    uint32_t    bus_id;
    uint16_t    address;
} pbus_i2c_channel_t;

typedef struct {
    uint32_t    port;
} pbus_uart_t;

typedef struct {
    uint32_t clk;
} pbus_clk_t;

typedef struct {
    uint32_t    iommu_index;
    uint32_t    bti_id;
} pbus_bti_t;

typedef struct {
    const char* name;
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    const pbus_mmio_t* mmios;
    uint32_t mmio_count;
    const pbus_irq_t* irqs;
    uint32_t irq_count;
    const pbus_gpio_t* gpios;
    uint32_t gpio_count;
    const pbus_i2c_channel_t* i2c_channels;
    uint32_t i2c_channel_count;
    const pbus_uart_t* uarts;
    uint32_t uart_count;
    const pbus_clk_t* clks;
    uint32_t clk_count;
    const pbus_bti_t* btis;
    uint32_t bti_count;
} pbus_dev_t;

// flags for pbus_device_add()
enum {
    // Add the device but to not publish it to the devmgr until enabled with pbus_device_enable().
    PDEV_ADD_DISABLED = (1 << 0),
    // Add the device to run in platform bus devhost rather than in a new devhost.
    PDEV_ADD_PBUS_DEVHOST = (1 << 1),
};

typedef struct {
    zx_status_t (*set_protocol)(void* ctx, uint32_t proto_id, void* protocol);
    zx_status_t (*wait_protocol)(void* ctx, uint32_t proto_id);
    zx_status_t (*device_add)(void* ctx, const pbus_dev_t* dev, uint32_t flags);
    zx_status_t (*device_enable)(void* ctx, uint32_t vid, uint32_t pid, uint32_t did, bool enable);
    const char* (*get_board_name)(void* ctx);
} platform_bus_protocol_ops_t;

typedef struct {
    platform_bus_protocol_ops_t* ops;
    void* ctx;
} platform_bus_protocol_t;

static inline zx_status_t pbus_set_protocol(platform_bus_protocol_t* pbus,
                                            uint32_t proto_id, void* protocol) {
    return pbus->ops->set_protocol(pbus->ctx, proto_id, protocol);
}

// waits for the specified protocol to be made available by another driver
// calling pbus_set_protocol()
static inline zx_status_t pbus_wait_protocol(platform_bus_protocol_t* pbus, uint32_t proto_id) {
    return pbus->ops->wait_protocol(pbus->ctx, proto_id);
}

static inline zx_status_t pbus_device_add(platform_bus_protocol_t* pbus, const pbus_dev_t* dev,
                                          uint32_t flags) {
    return pbus->ops->device_add(pbus->ctx, dev, flags);
}

// Dynamically enables or disables a platform device by adding or removing it
// from the DDK device tree.
static inline zx_status_t pbus_device_enable(platform_bus_protocol_t* pbus, uint32_t vid,
                                             uint32_t pid, uint32_t did, bool enable) {
    return pbus->ops->device_enable(pbus->ctx, vid, pid, did, enable);
}

static inline const char* pbus_get_board_name(platform_bus_protocol_t* pbus) {
    return pbus->ops->get_board_name(pbus->ctx);
}

__END_CDECLS;
