// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>

#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/aml-mali.h>

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "astro.h"

static void aml_bus_release(void* ctx) {
    aml_bus_t* bus = ctx;
    free(bus);
}

static zx_protocol_device_t aml_bus_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_bus_release,
};

static const pbus_dev_t rtc_dev = {
    .name = "rtc",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_RTC_FALLBACK,
};

static uint32_t astro_get_board_rev(aml_bus_t* bus) {
    uint32_t board_rev;
    uint8_t id0, id1, id2;
    gpio_config_in(&bus->gpio, GPIO_HW_ID0, GPIO_NO_PULL);
    gpio_config_in(&bus->gpio, GPIO_HW_ID1, GPIO_NO_PULL);
    gpio_config_in(&bus->gpio, GPIO_HW_ID2, GPIO_NO_PULL);
    gpio_read(&bus->gpio, GPIO_HW_ID0, &id0);
    gpio_read(&bus->gpio, GPIO_HW_ID1, &id1);
    gpio_read(&bus->gpio, GPIO_HW_ID2, &id2);
    board_rev = id0 + (id1 << 1) + (id2 << 2);

    if (board_rev >= MAX_SUPPORTED_REV) {
        // We have detected a new board rev. Print this warning just in case the
        // new board rev requires additional support that we were not aware of
        zxlogf(INFO, "Unsupported board revision detected (%d)\n", board_rev);
    }

    return board_rev;
}

static int aml_start_thread(void* arg) {
    aml_bus_t* bus = arg;
    zx_status_t status;

    if ((status = aml_gpio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_gpio_init failed: %d\n", status);
        goto fail;
    }

    // Once gpio is up and running, let's populate board revision
    pbus_board_info_t info;
    info.board_revision = astro_get_board_rev(bus);
    pbus_set_board_info(&bus->pbus, &info);

    zxlogf(INFO, "Detected board rev 0x%x\n", info.board_revision);

    if ((status = aml_i2c_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_i2c_init failed: %d\n", status);
        goto fail;
    }

    status = aml_mali_init(&bus->pbus, BTI_MALI);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_mali_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_usb_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_usb_init failed: %d\n", status);
        goto fail;
    }

    if ((status = astro_touch_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "astro_touch_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_display_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_display_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_canvas_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_canvas_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_video_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_video_init failed: %d\n", status);
        goto fail;
    }

    if ((status = pbus_device_add(&bus->pbus, &rtc_dev)) != ZX_OK) {
        zxlogf(ERROR, "aml_start_thread could not add rtc_dev: %d\n", status);
        goto fail;
    }

    if ((status = aml_raw_nand_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_raw_nand_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_sdio_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_sdio_init failed: %d\n", status);
        goto fail;
    }

    if ((status = ams_light_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "ams_light_init failed: %d\n", status);
        goto fail;
    }

    // This function includes some non-trivial delays, so lets run this last
    // to avoid slowing down the rest of the boot.
    if ((status = aml_bluetooth_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_bluetooth_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_clk_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_clk_init failed: %d\n", status);
        goto fail;
    }

    if ((status = aml_thermal_init(bus)) != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init failed: %d\n", status);
        goto fail;
    }

    return ZX_OK;
fail:
    zxlogf(ERROR, "aml_start_thread failed, not all devices have been initialized\n");
    return status;
}

static zx_status_t aml_bus_bind(void* ctx, zx_device_t* parent) {
    aml_bus_t* bus = calloc(1, sizeof(aml_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }
    bus->parent = parent;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &bus->pbus);
    if (status != ZX_OK) {
        goto fail;
    }

    // get default BTI from the dummy IOMMU implementation in the platform bus
    status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &bus->iommu);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_bus_bind: could not get ZX_PROTOCOL_IOMMU\n");
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-bus",
        .ctx = bus,
        .ops = &aml_bus_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, NULL);
    if (status != ZX_OK) {
        goto fail;
    }

    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, aml_start_thread, bus, "aml_start_thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        goto fail;
    }
    return ZX_OK;

fail:
    zxlogf(ERROR, "aml_bus_bind failed %d\n", status);
    aml_bus_release(bus);
    return status;
}

static zx_driver_ops_t aml_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_bus_bind,
};

ZIRCON_DRIVER_BEGIN(aml_bus, aml_bus_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_BUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_ASTRO),
ZIRCON_DRIVER_END(aml_bus)
