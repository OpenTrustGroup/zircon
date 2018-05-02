// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/usb-mode-switch.h>

#include "platform-proxy.h"

typedef struct {
    zx_device_t* zxdev;
    zx_handle_t rpc_channel;
    atomic_int next_txid;
} platform_proxy_t;

static zx_status_t platform_dev_rpc(platform_proxy_t* proxy, pdev_req_t* req, uint32_t req_length,
                                    pdev_resp_t* resp, uint32_t resp_length,
                                    zx_handle_t* out_handles, uint32_t out_handle_count,
                                    uint32_t* out_data_received) {
    uint32_t resp_size, handle_count;

    req->txid = atomic_fetch_add(&proxy->next_txid, 1);

    zx_channel_call_args_t args = {
        .wr_bytes = req,
        .rd_bytes = resp,
        .wr_num_bytes = req_length,
        .rd_num_bytes = resp_length,
        .rd_handles = out_handles,
        .rd_num_handles = out_handle_count,
    };
    zx_status_t status = zx_channel_call(proxy->rpc_channel, 0, ZX_TIME_INFINITE, &args, &resp_size,
                                         &handle_count, NULL);
    if (status != ZX_OK) {
        return status;
    } else if (resp_size < sizeof(*resp)) {
        zxlogf(ERROR, "platform_dev_rpc resp_size too short: %u\n", resp_size);
        status = ZX_ERR_INTERNAL;
        goto fail;
    } else if (handle_count != out_handle_count) {
        zxlogf(ERROR, "platform_dev_rpc handle count %u expected %u\n", handle_count,
                out_handle_count);
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    status = resp->status;
    if (out_data_received) {
        *out_data_received = resp_size - sizeof(pdev_resp_t);
    }

fail:
    if (status != ZX_OK) {
        for (uint32_t i = 0; i < handle_count; i++) {
            zx_handle_close(out_handles[i]);
        }
    }
    return status;
}

static zx_status_t pdev_ums_get_initial_mode(void* ctx, usb_mode_t* out_mode) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_UMS_GET_INITIAL_MODE,
    };
    pdev_resp_t resp;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0,
                                          NULL);
    if (status != ZX_OK) {
        return status;
    }
    *out_mode = resp.usb_mode;
    return ZX_OK;
}

static zx_status_t pdev_ums_set_mode(void* ctx, usb_mode_t mode) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_UMS_SET_MODE,
        .usb_mode = mode,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0, NULL);
}

usb_mode_switch_protocol_ops_t usb_mode_switch_ops = {
    .get_initial_mode = pdev_ums_get_initial_mode,
    .set_mode = pdev_ums_set_mode,
};

static zx_status_t pdev_gpio_config(void* ctx, uint32_t index, uint32_t flags) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GPIO_CONFIG,
        .index = index,
        .gpio_flags = flags,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0, NULL);
}

static zx_status_t pdev_gpio_set_alt_function(void* ctx, uint32_t index, uint64_t function) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GPIO_SET_ALT_FUNCTION,
        .index = index,
        .gpio_alt_function = function,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0, NULL);
}

static zx_status_t pdev_gpio_get_interrupt(void* ctx, uint32_t index,
                                           uint32_t flags,
                                           zx_handle_t *out_handle) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GPIO_GET_INTERRUPT,
        .index = index,
        .flags = flags,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                          out_handle, 1, NULL);
}

static zx_status_t pdev_gpio_set_polarity(void* ctx, uint32_t index, uint32_t polarity) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GPIO_SET_POLARITY,
        .index = index,
        .flags = polarity,
    };
    pdev_resp_t resp;
    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0, NULL);
}
static zx_status_t pdev_gpio_release_interrupt(void *ctx, uint32_t index) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GPIO_RELEASE_INTERRUPT,
        .index = index,
    };
    pdev_resp_t resp;
    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0, NULL);
}

static zx_status_t pdev_gpio_read(void* ctx, uint32_t index, uint8_t* out_value) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GPIO_READ,
        .index = index,
    };
    pdev_resp_t resp;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0,
                                          NULL);
    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.gpio_value;
    return ZX_OK;
}

static zx_status_t pdev_gpio_write(void* ctx, uint32_t index, uint8_t value) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GPIO_WRITE,
        .index = index,
        .gpio_value = value,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0, NULL);
}

static gpio_protocol_ops_t gpio_ops = {
    .config = pdev_gpio_config,
    .set_alt_function = pdev_gpio_set_alt_function,
    .read = pdev_gpio_read,
    .write = pdev_gpio_write,
    .get_interrupt = pdev_gpio_get_interrupt,
    .release_interrupt = pdev_gpio_release_interrupt,
    .set_polarity = pdev_gpio_set_polarity,
};

static zx_status_t pdev_i2c_get_max_transfer_size(void* ctx, uint32_t index, size_t* out_size) {
    platform_proxy_t* proxy = ctx;

    pdev_req_t req = {
        .op = PDEV_I2C_GET_MAX_TRANSFER,
        .index = index,
    };
    pdev_resp_t resp;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0,
                                          NULL);
    if (status == ZX_OK) {
        *out_size = resp.i2c_max_transfer;
    }
    return status;
}

static zx_status_t pdev_i2c_transact(void* ctx, uint32_t index, const void* write_buf,
                                     size_t write_length, size_t read_length,
                                     i2c_complete_cb complete_cb, void* cookie) {
    platform_proxy_t* proxy = ctx;

    if (!read_length && !write_length) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (write_length > PDEV_I2C_MAX_TRANSFER_SIZE ||
        read_length > PDEV_I2C_MAX_TRANSFER_SIZE) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    struct {
        pdev_req_t req;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } req = {
        .req = {
            .op = PDEV_I2C_TRANSACT,
            .index = index,
            .i2c_txn = {
                .write_length = write_length,
                .read_length = read_length,
                .complete_cb = complete_cb,
                .cookie = cookie,
            },
        },
    };
    struct {
        pdev_resp_t resp;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } resp;

    if (write_length) {
        memcpy(req.data, write_buf, write_length);
    }
    uint32_t data_received;
    zx_status_t status = platform_dev_rpc(proxy, &req.req,
                                          sizeof(req.req) + write_length, &resp.resp, sizeof(resp),
                                          NULL, 0, &data_received);
    if (status != ZX_OK) {
        return status;
    }

    // TODO(voydanoff) This proxying code actually implements i2c_transact synchronously
    // due to the fact that it is unsafe to respond asynchronously on the devmgr rxrpc channel.
    // In the future we may want to redo the plumbing to allow this to be truly asynchronous.
    if (data_received != read_length) {
        status = ZX_ERR_INTERNAL;
    } else {
        status = resp.resp.status;
    }
    if (complete_cb) {
        complete_cb(status, resp.data, resp.resp.i2c_txn.cookie);
    }

    return ZX_OK;
}

static void pdev_i2c_channel_release(void* ctx) {
    free(ctx);
}

static i2c_protocol_ops_t i2c_ops = {
    .transact = pdev_i2c_transact,
    .get_max_transfer_size = pdev_i2c_get_max_transfer_size,
};

static zx_status_t pdev_clk_enable(void* ctx, uint32_t index) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_CLK_ENABLE,
        .index = index,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL,
                            0, NULL);
}

static zx_status_t pdev_clk_disable(void* ctx, uint32_t index) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_CLK_DISABLE,
        .index = index,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL,
                            0, NULL);
}

static clk_protocol_ops_t clk_ops = {
    .enable = pdev_clk_enable,
    .disable = pdev_clk_disable,
};

static zx_status_t platform_dev_map_mmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                         void** vaddr, size_t* size, zx_handle_t* out_handle) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GET_MMIO,
        .index = index,
    };
    pdev_resp_t resp;
    zx_handle_t vmo_handle;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp),
                                          &vmo_handle, 1, NULL);
    if (status != ZX_OK) {
        return status;
    }

    size_t vmo_size;
    status = zx_vmo_get_size(vmo_handle, &vmo_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_get_size failed %d\n", status);
        goto fail;
    }

    status = zx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_set_cache_policy failed %d\n", status);
        goto fail;
    }

    uintptr_t virt;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         &virt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmar_map failed %d\n", status);
        goto fail;
    }

    *size = resp.mmio.length;
    *out_handle = vmo_handle;
    *vaddr = (void *)(virt + resp.mmio.offset);
    return ZX_OK;

fail:
    zx_handle_close(vmo_handle);
    return status;
}

static zx_status_t platform_dev_map_interrupt(void* ctx, uint32_t index,
                                              uint32_t flags, zx_handle_t* out_handle) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GET_INTERRUPT,
        .index = index,
        .flags = flags,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), out_handle, 1, NULL);
}

static zx_status_t platform_dev_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GET_BTI,
        .index = index,
    };
    pdev_resp_t resp;

    return platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), out_handle, 1, NULL);
}

static zx_status_t platform_dev_get_device_info(void* ctx, pdev_device_info_t* out_info) {
    platform_proxy_t* proxy = ctx;
    pdev_req_t req = {
        .op = PDEV_GET_DEVICE_INFO,
    };
    pdev_resp_t resp;

    zx_status_t status = platform_dev_rpc(proxy, &req, sizeof(req), &resp, sizeof(resp), NULL, 0,
                                          NULL);
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.info, sizeof(*out_info));
    return ZX_OK;
}

static platform_device_protocol_ops_t platform_dev_proto_ops = {
    .map_mmio = platform_dev_map_mmio,
    .map_interrupt = platform_dev_map_interrupt,
    .get_bti = platform_dev_get_bti,
    .get_device_info = platform_dev_get_device_info,
};

static zx_status_t platform_dev_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    switch (proto_id) {
    case ZX_PROTOCOL_PLATFORM_DEV: {
        platform_device_protocol_t* proto = out;
        proto->ctx = ctx;
        proto->ops = &platform_dev_proto_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        usb_mode_switch_protocol_t* proto = out;
        proto->ctx = ctx;
        proto->ops = &usb_mode_switch_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_GPIO: {
        gpio_protocol_t* proto = out;
        proto->ctx = ctx;
        proto->ops = &gpio_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_I2C: {
        i2c_protocol_t* proto = out;
        proto->ctx = ctx;
        proto->ops = &i2c_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_CLK: {
        clk_protocol_t* proto = out;
        proto->ctx = ctx;
        proto->ops = &clk_ops;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void platform_dev_release(void* ctx) {
    platform_proxy_t* proxy = ctx;

    zx_handle_close(proxy->rpc_channel);
    free(proxy);
}

static zx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = platform_dev_get_protocol,
    .release = platform_dev_release,
};

zx_status_t platform_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel) {
    platform_proxy_t* proxy = calloc(1, sizeof(platform_proxy_t));
    if (!proxy) {
        return ZX_ERR_NO_MEMORY;
    }
    proxy->rpc_channel = rpc_channel;

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = proxy,
        .ops = &platform_dev_proto,
        .proto_id = ZX_PROTOCOL_PLATFORM_DEV,
        .proto_ops = &platform_dev_proto_ops,
    };

    zx_status_t status = device_add(parent, &add_args, &proxy->zxdev);
    if (status != ZX_OK) {
        zx_handle_close(rpc_channel);
        free(proxy);
    }

    return status;
}

static zx_driver_ops_t platform_bus_proxy_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = platform_proxy_create,
};

ZIRCON_DRIVER_BEGIN(platform_bus_proxy, platform_bus_proxy_driver_ops, "zircon", "0.1", 1)
    // devmgr loads us directly, so we need no binding information here
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus_proxy)
