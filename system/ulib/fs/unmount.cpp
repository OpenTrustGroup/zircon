// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Sends an 'unmount' signal on the srv handle, and waits until it is closed.
// Consumes 'srv'.
zx_status_t vfs_unmount_handle(zx_handle_t srv, zx_time_t deadline) {
    // TODO(smklein): Use C Bindings.
    uint8_t msg[FIDL_ALIGN(sizeof(fuchsia_io_DirectoryAdminUnmountRequest)) + FDIO_CHUNK_SIZE];
    auto request = reinterpret_cast<fuchsia_io_DirectoryAdminUnmountRequest*>(msg);
    auto response = reinterpret_cast<fuchsia_io_DirectoryAdminUnmountResponse*>(msg);

    // the only other messages we ever send are no-reply OPEN or CLONE with
    // txid of 0.
    request->hdr.txid = 1;
    request->hdr.ordinal = fuchsia_io_DirectoryAdminUnmountOrdinal;

    zx_channel_call_args_t args;
    args.wr_bytes = request;
    args.wr_handles = NULL;
    args.rd_bytes = response;
    args.rd_handles = NULL;
    args.wr_num_bytes = FIDL_ALIGN(sizeof(fuchsia_io_DirectoryAdminUnmountRequest));
    args.wr_num_handles = 0;
    args.rd_num_bytes = FIDL_ALIGN(sizeof(fuchsia_io_DirectoryAdminUnmountResponse));
    args.rd_num_handles = 0;

    uint32_t dsize;
    uint32_t hcount;

    // At the moment, we don't actually care what the response is from the
    // filesystem server (or even if it supports the unmount operation). As
    // soon as ANY response comes back, either in the form of a closed handle
    // or a visible response, shut down.
    zx_status_t status = zx_channel_call(srv, 0, deadline, &args, &dsize, &hcount);
    if (status == ZX_OK) {
        // Read phase succeeded. If the target filesystem returned an error, we
        // should parse it.
        if (dsize < FIDL_ALIGN(sizeof(fuchsia_io_DirectoryAdminUnmountResponse))) {
            status = ZX_ERR_IO;
        } else {
            status = response->s;
        }
    }
    zx_handle_close(srv);
    return status;
}
