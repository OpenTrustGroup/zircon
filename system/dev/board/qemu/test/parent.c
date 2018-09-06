// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include "../qemu-virt.h"

#define DRIVER_NAME "qemu-test-parent"

typedef struct {
    zx_device_t* zxdev;
} qemu_test_t;

static void qemu_test_release(void* ctx) {
    free(ctx);
}

static zx_protocol_device_t qemu_test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = qemu_test_release,
};

static zx_status_t qemu_test_bti(platform_device_protocol_t* pdev) {
    zx_status_t status;
    zx_handle_t bti;

    status = pdev_get_bti(pdev, 0, &bti);
    if (status == ZX_OK) {
        // Parent doesn't have any BTIs so this call should have failed.
        zxlogf(ERROR, "%s: parent got btis it doesn't own!\n", DRIVER_NAME);
        zx_handle_close(bti);
        return ZX_ERR_INTERNAL;
    }

    // Treat any error as a success.
    return ZX_OK;
}

static zx_status_t qemu_test_bind(void* ctx, zx_device_t* parent) {
    platform_device_protocol_t pdev;
    zx_status_t status;

    zxlogf(INFO, "qemu_test_bind: %s \n", DRIVER_NAME);

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_PLATFORM_DEV\n", DRIVER_NAME);
        return status;
    }

    // Make sure we can access our MMIO.
    io_buffer_t mmio;
    status = pdev_map_mmio_buffer(&pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer failed\n", DRIVER_NAME);
        return status;
    }
    if (mmio.size != TEST_MMIO_1_SIZE) {
        zxlogf(ERROR, "%s: mmio.size expected %u got %zu\n", DRIVER_NAME, TEST_MMIO_1_SIZE,
               mmio.size);
    }
    io_buffer_release(&mmio);

    status = qemu_test_bti(&pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: bti test failed, st = %d\n", DRIVER_NAME, status);
    }

    qemu_test_t* test = calloc(1, sizeof(qemu_test_t));
    if (!test) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_device_prop_t child_props[] = {
        { BIND_PROTOCOL, 0, ZX_PROTOCOL_PLATFORM_DEV },
        { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_QEMU },
        { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_QEMU },
        { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_QEMU_TEST_CHILD_1 },
    };

    device_add_args_t child_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "child-1",
        .ctx = test,
        .ops = &qemu_test_device_protocol,
        .props = child_props,
        .prop_count = countof(child_props),
    };

    status = pdev_device_add(&pdev, 0, &child_args, &test->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_device_add failed: %d\n", DRIVER_NAME, status);
        free(test);
        return status;
    }

    return ZX_OK;
}

static zx_driver_ops_t qemu_test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = qemu_test_bind,
};

ZIRCON_DRIVER_BEGIN(qemu_bus, qemu_test_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QEMU),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_QEMU),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_QEMU_TEST_PARENT),
ZIRCON_DRIVER_END(qemu_bus)
