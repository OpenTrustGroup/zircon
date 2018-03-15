// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs.h>
#include <fdio/debug.h>
#include <fdio/io.fidl2.h>
#include <fdio/remoteio.h>
#include <fdio/vfs.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Sends an 'unmount' signal on the srv handle, and waits until it is closed.
// Consumes 'srv'.
zx_status_t vfs_unmount_handle(zx_handle_t srv, zx_time_t deadline) {
    // TODO(smklein): use shared ioctl impl?
#ifdef ZXRIO_FIDL
    uint8_t msg[FIDL_ALIGN(sizeof(NodeIoctlMsg)) + FDIO_CHUNK_SIZE];
    NodeIoctlMsg* request = (NodeIoctlMsg*) msg;
    NodeIoctlRsp* response = (NodeIoctlRsp*) msg;

    // the only other messages we ever send are no-reply OPEN or CLONE with
    // txid of 0.
    request->hdr.txid = 1;
    request->hdr.ordinal = ZXFIDL_IOCTL;
    request->opcode = IOCTL_VFS_UNMOUNT_FS;
    request->max_out = 0;
    request->handles.count = 0;
    request->handles.data = (void*) FIDL_ALLOC_PRESENT;
    request->in.count = 0;
    request->in.data = (void*) FIDL_ALLOC_PRESENT;

    zx_channel_call_args_t args;
    args.wr_bytes = request;
    args.wr_handles = NULL;
    args.rd_bytes = response;
    args.rd_handles = NULL;
    args.wr_num_bytes = FIDL_ALIGN(sizeof(NodeIoctlMsg));
    args.wr_num_handles = 0;
    args.rd_num_bytes = FIDL_ALIGN(sizeof(NodeIoctlRsp));
    args.rd_num_handles = 0;

    uint32_t dsize;
    uint32_t hcount;
    zx_status_t rs;

    // At the moment, we don't actually care what the response is from the
    // filesystem server (or even if it supports the unmount operation). As
    // soon as ANY response comes back, either in the form of a closed handle
    // or a visible response, shut down.
    zx_status_t status = zx_channel_call(srv, 0, deadline, &args, &dsize, &hcount, &rs);
    if (status == ZX_ERR_CALL_FAILED) {
        // Write phase succeeded. The target filesystem had a chance to unmount properly.
        status = ZX_OK;
    } else if (status == ZX_OK) {
        // Read phase succeeded. If the target filesystem returned an error, we
        // should parse it.
        if (dsize < FIDL_ALIGN(sizeof(NodeIoctlRsp))) {
            status = ZX_ERR_IO;
        } else {
            status = response->s;
        }
    }
#else
    zxrio_msg_t msg;
    memset(&msg, 0, ZXRIO_HDR_SZ);

    // the only other messages we ever send are no-reply OPEN or CLONE with
    // txid of 0.
    msg.txid = 1;
    msg.op = ZXRIO_IOCTL;
    msg.arg2.op = IOCTL_VFS_UNMOUNT_FS;

    zx_channel_call_args_t args;
    args.wr_bytes = &msg;
    args.wr_handles = NULL;
    args.rd_bytes = &msg;
    args.rd_handles = NULL;
    args.wr_num_bytes = ZXRIO_HDR_SZ;
    args.wr_num_handles = 0;
    args.rd_num_bytes = ZXRIO_HDR_SZ + FDIO_CHUNK_SIZE;
    args.rd_num_handles = 0;

    uint32_t dsize;
    uint32_t hcount;
    zx_status_t rs;

    // At the moment, we don't actually care what the response is from the
    // filesystem server (or even if it supports the unmount operation). As
    // soon as ANY response comes back, either in the form of a closed handle
    // or a visible response, shut down.
    zx_status_t status = zx_channel_call(srv, 0, deadline, &args, &dsize, &hcount, &rs);
    if (status == ZX_ERR_CALL_FAILED) {
        // Write phase succeeded. The target filesystem had a chance to unmount properly.
        status = ZX_OK;
    } else if (status == ZX_OK) {
        // Read phase succeeded. If the target filesystem returned an error, we
        // should parse it.
        if (dsize < ZXRIO_HDR_SZ) {
            status = ZX_ERR_IO;
        } else {
            status = msg.arg;
        }
    }
#endif
    zx_handle_close(srv);
    return status;
}
