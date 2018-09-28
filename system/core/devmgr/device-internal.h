// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef struct proxy_iostate proxy_iostate_t;

struct zx_device {
    zx_device() = default;
    ~zx_device() = default;

    zx_device(const zx_device&) = delete;
    zx_device& operator=(const zx_device&) = delete;

    zx_device(zx_device&&) = default;
    zx_device& operator=(zx_device&&) = default;

    zx_status_t Open(zx_device_t** dev_out, uint32_t flags) {
        return ops->open(ctx, dev_out, flags);
    }

    zx_status_t OpenAt(zx_device_t** dev_out, const char* path, uint32_t flags) {
        return ops->open_at(ctx, dev_out, path, flags);
    }

    zx_status_t Close(uint32_t flags) {
        return ops->close(ctx, flags);
    }

    void Unbind() {
        ops->unbind(ctx);
    }

    void Release() {
        ops->release(ctx);
    }

    zx_status_t Suspend(uint32_t flags) {
        return ops->suspend(ctx, flags);
    }

    zx_status_t Resume(uint32_t flags) {
        return ops->resume(ctx, flags);
    }

    zx_status_t Read(void* buf, size_t count, zx_off_t off,
                     size_t* actual) {
        return ops->read(ctx, buf, count, off, actual);
    }

    zx_status_t Write(const void* buf, size_t count,
                      zx_off_t off, size_t* actual) {
        return ops->write(ctx, buf, count, off, actual);
    }

    zx_off_t GetSize() {
        return ops->get_size(ctx);
    }

    zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                      void* out_buf, size_t out_len, size_t* out_actual) {
        return ops->ioctl(ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
    }

    zx_status_t Message(fidl_msg_t* msg, fidl_txn_t* txn) {
        return ops->message(ctx, msg, txn);
    }

    uintptr_t magic;

    zx_protocol_device_t* ops;

    // reserved for driver use; will not be touched by devmgr
    void* ctx;

    uint32_t flags;
    uint32_t refcount;

    zx_handle_t event;
    zx_handle_t local_event;
    zx_handle_t rpc;

    // most devices implement a single
    // protocol beyond the base device protocol
    uint32_t protocol_id;
    void* protocol_ops;

    // driver that has published this device
    zx_driver_t* driver;

    // parent in the device tree
    zx_device_t* parent;

    // for the parent's device_list
    struct list_node node;

    // list of this device's children in the device tree
    struct list_node children;

    // list node for the defer_device_list
    struct list_node defer;

    // iostate
    void* ios;
    proxy_iostate_t* proxy_ios;

    char name[ZX_DEVICE_NAME_MAX + 1];
};

// zx_device_t objects must be created or initialized by the driver manager's
// device_create() function.  Drivers MAY NOT touch any
// fields in the zx_device_t, except for the protocol_id and protocol_ops
// fields which it may fill out after init and before device_add() is called,
// and the ctx field which may be used to store driver-specific data.

#define DEV_FLAG_DEAD           0x00000001  // being deleted
#define DEV_FLAG_VERY_DEAD      0x00000002  // safe for ref0 and release()
#define DEV_FLAG_UNBINDABLE     0x00000004  // nobody may bind to this device
#define DEV_FLAG_BUSY           0x00000010  // device being created
#define DEV_FLAG_INSTANCE       0x00000020  // this device was created-on-open
#define DEV_FLAG_MULTI_BIND     0x00000080  // this device accepts many children
#define DEV_FLAG_ADDED          0x00000100  // device_add() has been called for this device
#define DEV_FLAG_INVISIBLE      0x00000200  // device not visible via devfs
#define DEV_FLAG_UNBOUND        0x00000400  // informed that it should self-delete asap
#define DEV_FLAG_WANTS_REBIND   0x00000800  // when last child goes, rebind this device

#define DEV_MAGIC 'MDEV'

zx_status_t device_bind(zx_device_t* dev, const char* drv_libname);
zx_status_t device_open_at(zx_device_t* dev, zx_device_t** out, const char* path, uint32_t flags);
zx_status_t device_close(zx_device_t* dev, uint32_t flags);

__END_CDECLS
