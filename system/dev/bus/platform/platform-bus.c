// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <zircon/process.h>
#include <zircon/syscalls/iommu.h>

#include "platform-bus.h"

static zx_status_t platform_bus_get_bti(void* ctx, uint32_t iommu_index, uint32_t bti_id,
                                        zx_handle_t* out_handle) {
    platform_bus_t* bus = ctx;
    if (iommu_index != 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_bti_create(bus->dummy_iommu_handle, 0, bti_id, out_handle);
}

// default IOMMU protocol to use if the board driver does not set one via pbus_set_protocol()
static iommu_protocol_ops_t platform_bus_default_iommu_ops = {
    .get_bti = platform_bus_get_bti,
};

static zx_status_t platform_bus_set_protocol(void* ctx, uint32_t proto_id, void* protocol) {
    if (!protocol) {
        return ZX_ERR_INVALID_ARGS;
    }
    platform_bus_t* bus = ctx;

    switch (proto_id) {
    case ZX_PROTOCOL_USB_MODE_SWITCH:
        memcpy(&bus->ums, protocol, sizeof(bus->ums));
        break;
    case ZX_PROTOCOL_GPIO:
        memcpy(&bus->gpio, protocol, sizeof(bus->gpio));
        break;
    case ZX_PROTOCOL_I2C:
        memcpy(&bus->i2c, protocol, sizeof(bus->i2c));
        break;
    case ZX_PROTOCOL_CLK:
        memcpy(&bus->clk, protocol, sizeof(bus->clk));
        break;
    case ZX_PROTOCOL_SERIAL_IMPL: {
        zx_status_t status = platform_serial_init(bus, (serial_impl_protocol_t *)protocol);
        if (status != ZX_OK) {
            return status;
         }
        memcpy(&bus->serial, protocol, sizeof(bus->serial));
        break;
    }
    case ZX_PROTOCOL_IOMMU:
        memcpy(&bus->iommu, protocol, sizeof(bus->iommu));
        break;
    default:
        // TODO(voydanoff) consider having a registry of arbitrary protocols
        return ZX_ERR_NOT_SUPPORTED;
    }

    completion_signal(&bus->proto_completion);
    return ZX_OK;
}

static zx_status_t platform_bus_wait_protocol(void* ctx, uint32_t proto_id) {
    platform_bus_t* bus = ctx;

    platform_bus_protocol_t dummy;
    while (platform_bus_get_protocol(bus, proto_id, &dummy) == ZX_ERR_NOT_SUPPORTED) {
        completion_reset(&bus->proto_completion);
        zx_status_t status = completion_wait(&bus->proto_completion, ZX_TIME_INFINITE);
        if (status != ZX_OK) {
            return status;
        }
    }
    return ZX_OK;
}

static zx_status_t platform_bus_device_add(void* ctx, const pbus_dev_t* dev, uint32_t flags) {
    platform_bus_t* bus = ctx;
    return platform_device_add(bus, dev, flags);
}

static zx_status_t platform_bus_device_enable(void* ctx, uint32_t vid, uint32_t pid, uint32_t did,
                                              bool enable) {
    platform_bus_t* bus = ctx;
    platform_dev_t* dev;
    list_for_every_entry(&bus->devices, dev, platform_dev_t, node) {
        if (dev->vid == vid && dev->pid == pid && dev->did == did) {
            return platform_device_enable(dev, enable);
        }
    }

    return ZX_ERR_NOT_FOUND;
}

static const char* platform_bus_get_board_name(void* ctx) {
    platform_bus_t* bus = ctx;
    return bus->board_name;
}

static platform_bus_protocol_ops_t platform_bus_proto_ops = {
    .set_protocol = platform_bus_set_protocol,
    .wait_protocol = platform_bus_wait_protocol,
    .device_add = platform_bus_device_add,
    .device_enable = platform_bus_device_enable,
    .get_board_name = platform_bus_get_board_name,
};

// not static so we can access from platform_dev_get_protocol()
zx_status_t platform_bus_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
    platform_bus_t* bus = ctx;

    switch (proto_id) {
    case ZX_PROTOCOL_PLATFORM_BUS: {
        platform_bus_protocol_t* proto = protocol;
        proto->ops = &platform_bus_proto_ops;
        proto->ctx = bus;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH:
        if (bus->ums.ops) {
            memcpy(protocol, &bus->ums, sizeof(bus->ums));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_GPIO:
        if (bus->gpio.ops) {
            memcpy(protocol, &bus->gpio, sizeof(bus->gpio));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_I2C:
        if (bus->i2c.ops) {
            memcpy(protocol, &bus->i2c, sizeof(bus->i2c));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_CLK:
        if (bus->clk.ops) {
            memcpy(protocol, &bus->clk, sizeof(bus->clk));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_SERIAL_IMPL:
        if (bus->serial.ops) {
            memcpy(protocol, &bus->serial, sizeof(bus->serial));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_IOMMU:
        if (bus->iommu.ops) {
            memcpy(protocol, &bus->iommu, sizeof(bus->iommu));
            return ZX_OK;
        }
        break;
    default:
        // TODO(voydanoff) consider having a registry of arbitrary protocols
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static void platform_bus_release(void* ctx) {
    platform_bus_t* bus = ctx;

    platform_dev_t* dev;
    list_for_every_entry(&bus->devices, dev, platform_dev_t, node) {
        platform_dev_free(dev);
    }

    i2c_txn_t* txn;
    i2c_txn_t* temp;
    list_for_every_entry_safe(&bus->i2c_txns, txn, temp, i2c_txn_t, node) {
        free(txn);
    }

    platform_serial_release(bus);
    zx_handle_close(bus->dummy_iommu_handle);

    free(bus);
}

static zx_protocol_device_t platform_bus_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = platform_bus_get_protocol,
    .release = platform_bus_release,
};

static zx_status_t sys_device_suspend(void* ctx, uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_protocol_device_t sys_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .suspend = sys_device_suspend,
};

static zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name,
                                       const char* args, zx_handle_t rpc_channel) {
    if (!args) {
        zxlogf(ERROR, "platform_bus_create: args missing\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint32_t vid = 0;
    uint32_t pid = 0;
    if (sscanf(args, "vid=%u,pid=%u", &vid, &pid) != 2) {
        zxlogf(ERROR, "platform_bus_create: could not find vid or pid in args\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    platform_bus_t* bus = calloc(1, sizeof(platform_bus_t));
    if (!bus) {
        return  ZX_ERR_NO_MEMORY;
    }
    completion_reset(&bus->proto_completion);
    bus->resource = get_root_resource();

    // set up a dummy IOMMU protocol to use in the case where our board driver does not
    // set a real one.
    zx_iommu_desc_dummy_t desc;
    zx_status_t status = zx_iommu_create(bus->resource, ZX_IOMMU_TYPE_DUMMY,
                                         &desc, sizeof(desc), &bus->dummy_iommu_handle);
    if (status != ZX_OK) {
        free(bus);
        return status;
    }
    bus->iommu.ops = &platform_bus_default_iommu_ops;
    bus->iommu.ctx = bus;

    char* board_name = strstr(args, "board=");
    if (board_name) {
        board_name += strlen("board=");
        strncpy(bus->board_name, board_name, sizeof(bus->board_name));
        bus->board_name[sizeof(bus->board_name) - 1] = 0;
        char* comma = strchr(bus->board_name, ',');
        if (comma) {
            *comma = 0;
        }
    }

    // This creates the "sys" device
    device_add_args_t self_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ops = &sys_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &self_args, &parent);
    if (status != ZX_OK) {
        zx_handle_close(bus->dummy_iommu_handle);
        free(bus);
        return status;
    }

    // Then we attach the platform-bus device below it
    bus->vid = vid;
    bus->pid = pid;
    list_initialize(&bus->devices);
    list_initialize(&bus->i2c_txns);
    mtx_init(&bus->i2c_txn_lock, mtx_plain);

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, bus->vid},
        {BIND_PLATFORM_DEV_PID, 0, bus->pid},
    };

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "platform",
        .ctx = bus,
        .ops = &platform_bus_proto,
        .proto_id = ZX_PROTOCOL_PLATFORM_BUS,
        .proto_ops = &platform_bus_proto_ops,
        .props = props,
        .prop_count = countof(props),
    };

   return device_add(parent, &add_args, &bus->zxdev);
}

static zx_driver_ops_t platform_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = platform_bus_create,
};

ZIRCON_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "zircon", "0.1", 1)
    // devmgr loads us directly, so we need no binding information here
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus)
