// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/serial.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <limits.h>

__BEGIN_CDECLS;

typedef struct {
    uint32_t flags;
    uint32_t vid;
    uint32_t pid;
    uint32_t did;
    serial_port_info_t serial_port_info;
    uint32_t mmio_count;
    uint32_t irq_count;
    uint32_t gpio_count;
    uint32_t i2c_channel_count;
    uint32_t clk_count;
    uint32_t bti_count;
    uint32_t reserved[8];
} pdev_device_info_t;

// Interrupt description returned by pdev_get_irq_info(). The fields mirror
// pbus_irq_t.
typedef struct {
    uint32_t irq;
    uint32_t mode;  // ZX_INTERRUPT_MODE_* flags
} pdev_irq_t;

// MMIO region description returned by pdev_get_mmio_info(). The fields mirror
// pbus_mmio_t.
typedef struct {
    zx_paddr_t base;
    size_t length;
} pdev_mmio_t;

// I2C channel description returned by pdev_get_i2c_channels_info(). The
// fields mirror pbus_i2c_channel_t.
typedef struct {
    uint32_t bus_id;
    uint16_t address;
} pdev_i2c_channel_t;

typedef struct {
    zx_status_t (*map_mmio)(void* ctx, uint32_t index, uint32_t cache_policy, void** out_vaddr,
                            size_t* out_size, zx_handle_t* out_handle);
    zx_status_t (*map_interrupt)(void* ctx, uint32_t index, zx_handle_t* out_handle);
    zx_status_t (*get_bti)(void* ctx, uint32_t index, zx_handle_t* out_handle);
    zx_status_t (*get_device_info)(void* ctx, pdev_device_info_t* out_info);
    zx_status_t (*get_env_info)(void* ctx, const char* key, char* out_val);
    zx_status_t (*get_irq_info)(void* ctx, uint32_t index, pdev_irq_t* out_irq);
    zx_status_t (*get_mmio_info)(void* ctx, uint32_t index, pdev_mmio_t* out_mmio);
    zx_status_t (*get_i2c_channels_info)(void* ctx, uint32_t index,
                                         pdev_i2c_channel_t* out_i2c_channel);
} platform_device_protocol_ops_t;

typedef struct {
    platform_device_protocol_ops_t* ops;
    void* ctx;
} platform_device_protocol_t;

// Maps an MMIO region. "index" is relative to the list of MMIOs for the device.
static inline zx_status_t pdev_map_mmio(platform_device_protocol_t* pdev, uint32_t index,
                                        uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                                        zx_handle_t* out_handle) {
    return pdev->ops->map_mmio(pdev->ctx, index, cache_policy, out_vaddr, out_size, out_handle);
}

// Returns an interrupt handle. "index" is relative to the list of IRQs for the device.
static inline zx_status_t pdev_map_interrupt(platform_device_protocol_t* pdev, uint32_t index,
                                             zx_handle_t* out_handle) {
    return pdev->ops->map_interrupt(pdev->ctx, index, out_handle);
}

// Returns an IOMMU bus transaction initiator handle.
// "index" is relative to the list of BTIs for the device.
static inline zx_status_t pdev_get_bti(platform_device_protocol_t* pdev, uint32_t index,
                                       zx_handle_t* out_handle) {
    return pdev->ops->get_bti(pdev->ctx, index, out_handle);
}

static inline zx_status_t pdev_get_device_info(platform_device_protocol_t* pdev,
                                               pdev_device_info_t* out_info) {
    return pdev->ops->get_device_info(pdev->ctx, out_info);
}

// Looks up "key" in the environment of the platform bus devhost and copies
// its value into "out_val", which must hold at least PAGE_SIZE bytes.
static inline zx_status_t pdev_get_env_info(platform_device_protocol_t* pdev, const char* key,
                                            char* out_val) {
    return pdev->ops->get_env_info(pdev->ctx, key, out_val);
}

// Returns the interrupt number and mode of the IRQ at "index", without
// creating an interrupt handle.
static inline zx_status_t pdev_get_irq_info(platform_device_protocol_t* pdev, uint32_t index,
                                            pdev_irq_t* out_irq) {
    return pdev->ops->get_irq_info(pdev->ctx, index, out_irq);
}

// Returns the physical base and length of the MMIO region at "index",
// without mapping it.
static inline zx_status_t pdev_get_mmio_info(platform_device_protocol_t* pdev, uint32_t index,
                                             pdev_mmio_t* out_mmio) {
    return pdev->ops->get_mmio_info(pdev->ctx, index, out_mmio);
}

// Returns the bus ID and address of the I2C channel at "index".
static inline zx_status_t pdev_get_i2c_channels_info(platform_device_protocol_t* pdev,
                                                     uint32_t index,
                                                     pdev_i2c_channel_t* out_i2c_channel) {
    return pdev->ops->get_i2c_channels_info(pdev->ctx, index, out_i2c_channel);
}

// MMIO mapping helpers

static inline zx_status_t pdev_map_mmio_buffer(platform_device_protocol_t* pdev, uint32_t index,
                                               uint32_t cache_policy, io_buffer_t* buffer) {
    void* vaddr;
    size_t size;
    zx_handle_t vmo_handle;

    zx_status_t status = pdev_map_mmio(pdev, index, cache_policy, &vaddr, &size, &vmo_handle);
    if (status != ZX_OK) {
        return status;
    }
    zx_off_t offset = (uintptr_t)vaddr & (PAGE_SIZE - 1);
    vaddr = (void *)((uintptr_t)vaddr - offset);
    status = io_buffer_init_mmio(buffer, vmo_handle, vaddr, offset, size);
    zx_handle_close(vmo_handle);
    return status;
}

__END_CDECLS;
