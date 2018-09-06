// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/metadata.h>
#include <ddk/protocol/block.h>

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#define GUID_STRLEN 40

#define TXN_SIZE 0x4000 // 128 partition entries

typedef struct {
    zx_device_t* zxdev;
    zx_device_t* parent;

    block_protocol_t bp;
    zbi_partition_t part;

    block_info_t info;
    size_t block_op_size;
} bootpart_device_t;

struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

static void uint8_to_guid_string(char* dst, uint8_t* src) {
    struct guid* guid = (struct guid*)src;
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2,
            guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
            guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

static uint64_t get_lba_count(bootpart_device_t* dev) {
    // last LBA is inclusive
    return dev->part.last_block - dev->part.first_block + 1;
}

// implement device protocol:

static zx_status_t bootpart_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                                  void* reply, size_t max, size_t* out_actual) {
    bootpart_device_t* device = ctx;
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(info, &device->info, sizeof(*info));
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_TYPE_GUID: {
        char* guid = reply;
        if (max < ZBI_PARTITION_GUID_LEN) return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(guid, device->part.type_guid, ZBI_PARTITION_GUID_LEN);
        *out_actual = ZBI_PARTITION_GUID_LEN;
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_PARTITION_GUID: {
        char* guid = reply;
        if (max < ZBI_PARTITION_GUID_LEN) return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(guid, device->part.uniq_guid, ZBI_PARTITION_GUID_LEN);
        *out_actual = ZBI_PARTITION_GUID_LEN;
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = reply;
        strlcpy(name, device->part.name, max);
        *out_actual = strlen(name) + 1;
        return ZX_OK;
    }
    case IOCTL_DEVICE_SYNC: {
        // Propagate sync to parent device
        return device_ioctl(device->parent, IOCTL_DEVICE_SYNC, NULL, 0, NULL, 0, NULL);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void bootpart_query(void* ctx, block_info_t* bi, size_t* bopsz) {
    bootpart_device_t* bootpart = ctx;
    memcpy(bi, &bootpart->info, sizeof(block_info_t));
    *bopsz = bootpart->block_op_size;
}

static void bootpart_queue(void* ctx, block_op_t* bop) {
    bootpart_device_t* bootpart = ctx;

    switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
        size_t blocks = bop->rw.length;
        size_t max = get_lba_count(bootpart);

        // Ensure that the request is in-bounds
        if ((bop->rw.offset_dev >= max) ||
            ((max - bop->rw.offset_dev) < blocks)) {
            bop->completion_cb(bop, ZX_ERR_OUT_OF_RANGE);
            return;
        }

        // Adjust for partition starting block
        bop->rw.offset_dev += bootpart->part.first_block;
        break;
    }
    case BLOCK_OP_FLUSH:
        break;
    default:
        bop->completion_cb(bop, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    bootpart->bp.ops->queue(bootpart->bp.ctx, bop);
}

static void bootpart_unbind(void* ctx) {
    bootpart_device_t* device = ctx;
    device_remove(device->zxdev);
}

static void bootpart_release(void* ctx) {
    bootpart_device_t* device = ctx;
    free(device);
}

static zx_off_t bootpart_get_size(void* ctx) {
    bootpart_device_t* dev = ctx;
    //TODO: use query() results, *but* fvm returns different query and getsize
    // results, and the latter are dynamic...
    return device_get_size(dev->parent);
}

static zx_protocol_device_t device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = bootpart_ioctl,
    .get_size = bootpart_get_size,
    .unbind = bootpart_unbind,
    .release = bootpart_release,
};

static block_protocol_ops_t block_ops = {
    .query = bootpart_query,
    .queue = bootpart_queue,
};

static zx_status_t bootpart_bind(void* ctx, zx_device_t* parent) {
    block_protocol_t bp;
    uint8_t buffer[METADATA_PARTITION_MAP_MAX];
    size_t actual;

    if (device_get_protocol(parent, ZX_PROTOCOL_BLOCK, &bp) != ZX_OK) {
        zxlogf(ERROR, "bootpart: block device '%s': does not support block protocol\n",
               device_get_name(parent));
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = device_get_metadata(parent, DEVICE_METADATA_PARTITION_MAP, buffer,
                                             sizeof(buffer), &actual);
    if (status != ZX_OK) {
        return status;
    }

    zbi_partition_map_t* pmap = (zbi_partition_map_t*)buffer;
    if (pmap->partition_count == 0) {
        zxlogf(ERROR, "bootpart: partition_count is zero\n");
        return ZX_ERR_INTERNAL;
    }

    block_info_t block_info;
    size_t block_op_size;
    bp.ops->query(bp.ctx, &block_info, &block_op_size);

    for (unsigned i = 0; i < pmap->partition_count; i++) {
        zbi_partition_t* part = &pmap->partitions[i];
        char name[128];
        char type_guid[GUID_STRLEN];
        char uniq_guid[GUID_STRLEN];

        snprintf(name, sizeof(name), "part-%03u", i);
        uint8_to_guid_string(type_guid, part->type_guid);
        uint8_to_guid_string(uniq_guid, part->uniq_guid);

        zxlogf(SPEW, "bootpart: partition %u (%s) type=%s guid=%s name=%s first=0x%"
               PRIx64 " last=0x%" PRIx64 "\n", i, name, type_guid, uniq_guid, part->name,
               part->first_block, part->last_block);

        bootpart_device_t* device = calloc(1, sizeof(bootpart_device_t));
        if (!device) {
            return ZX_ERR_NO_MEMORY;
        }

        device->parent = parent;
        memcpy(&device->bp, &bp, sizeof(device->bp));
        memcpy(&device->part, part, sizeof(device->part));
        block_info.block_count = device->part.last_block - device->part.first_block + 1;
        memcpy(&device->info, &block_info, sizeof(block_info));
        device->block_op_size = block_op_size;

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = device,
            .ops = &device_proto,
            .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
            .proto_ops = &block_ops,
            .flags = DEVICE_ADD_INVISIBLE,
        };

        zx_status_t status = device_add(parent, &args, &device->zxdev);
        if (status != ZX_OK) {
            free(device);
            return status;
        }

        // add empty partition map metadata to prevent this driver from binding to its child devices
        status = device_add_metadata(device->zxdev, DEVICE_METADATA_PARTITION_MAP, NULL, 0);
        if (status != ZX_OK) {
            device_remove(device->zxdev);
            free(device);
            continue;
        }

        // make device visible after adding metadata
        device_make_visible(device->zxdev);
    }

    return ZX_OK;
}

static zx_driver_ops_t bootpart_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = bootpart_bind,
};

ZIRCON_DRIVER_BEGIN(bootpart, bootpart_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(bootpart)
