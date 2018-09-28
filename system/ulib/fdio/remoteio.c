// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <threads.h>

#include <zircon/assert.h>
#include <zircon/device/device.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>

#include "private-fidl.h"
#include "private-remoteio.h"

#define ZXDEBUG 0

// POLL_MASK and POLL_SHIFT intend to convert the lower five POLL events into
// ZX_USER_SIGNALs and vice-versa. Other events need to be manually converted to
// a zx_signals_t, if they are desired.
#define POLL_SHIFT  24
#define POLL_MASK   0x1F

static_assert(ZX_USER_SIGNAL_0 == (1 << POLL_SHIFT), "");
static_assert((POLLIN << POLL_SHIFT) == DEVICE_SIGNAL_READABLE, "");
static_assert((POLLPRI << POLL_SHIFT) == DEVICE_SIGNAL_OOB, "");
static_assert((POLLOUT << POLL_SHIFT) == DEVICE_SIGNAL_WRITABLE, "");
static_assert((POLLERR << POLL_SHIFT) == DEVICE_SIGNAL_ERROR, "");
static_assert((POLLHUP << POLL_SHIFT) == DEVICE_SIGNAL_HANGUP, "");

zx_handle_t zxrio_handle(zxrio_t* rio) {
    return rio->h;
}

// Acquire the additional handle from |info|.
//
// Returns |ZX_OK| if a handle was returned.
// Returns |ZX_ERR_NOT_FOUND| if no handle can be returned.
static zx_status_t zxrio_object_extract_handle(const zxrio_node_info_t* info,
                                               zx_handle_t* out) {
    switch (info->tag) {
    case fuchsia_io_NodeInfoTag_file:
        if (info->file.e != ZX_HANDLE_INVALID) {
            *out = info->file.e;
            return ZX_OK;
        }
        break;
    case fuchsia_io_NodeInfoTag_pipe:
        if (info->pipe.s != ZX_HANDLE_INVALID) {
            *out = info->pipe.s;
            return ZX_OK;
        }
        break;
    case fuchsia_io_NodeInfoTag_vmofile:
        if (info->vmofile.v != ZX_HANDLE_INVALID) {
            *out = info->vmofile.v;
            return ZX_OK;
        }
        break;
    case fuchsia_io_NodeInfoTag_device:
        if (info->device.e != ZX_HANDLE_INVALID) {
            *out = info->device.e;
            return ZX_OK;
        }
        break;
    }
    return ZX_ERR_NOT_FOUND;
}

static zx_status_t zxrio_close(fdio_t* io) {
    zxrio_t* rio = (zxrio_t*)io;

    zx_status_t r = fidl_close(rio);
    zx_handle_t h = rio->h;
    rio->h = ZX_HANDLE_INVALID;
    zx_handle_close(h);
    if (rio->event != ZX_HANDLE_INVALID) {
        h = rio->event;
        rio->event = ZX_HANDLE_INVALID;
        zx_handle_close(h);
    }
    return r;
}

// Open an object without waiting for the response.
// This function always consumes the cnxn handle
// The svc handle is only used to send a message
static zx_status_t zxrio_connect(zx_handle_t svc, zx_handle_t cnxn,
                                 uint32_t op, uint32_t flags, uint32_t mode,
                                 const char* name) {
    size_t len = strlen(name);
    if (len >= PATH_MAX) {
        zx_handle_close(cnxn);
        return ZX_ERR_BAD_PATH;
    }
    if (flags & ZX_FS_FLAG_DESCRIBE) {
        zx_handle_close(cnxn);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t r;
    switch (op) {
    case fuchsia_io_NodeCloneOrdinal:
        r = fidl_clone_request(svc, cnxn, flags);
        break;
    case fuchsia_io_DirectoryOpenOrdinal:
        r = fidl_open_request(svc, cnxn, flags, mode, name, len);
        break;
    default:
        zx_handle_close(cnxn);
        r = ZX_ERR_NOT_SUPPORTED;
    }
    return r;
}

static ssize_t zxrio_write(fdio_t* io, const void* data, size_t len) {
    zxrio_t* rio = (zxrio_t*) io;
    zx_status_t status = ZX_OK;
    uint64_t count = 0;
    uint64_t xfer;
    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;
        uint64_t actual = 0;
        if ((status = fidl_write(rio, data, xfer, &actual)) != ZX_OK) {
            return status;
        }
        count += actual;
        data += actual;
        len -= actual;
        if (xfer != actual) {
            break;
        }
    }
    if (count == 0) {
        return status;
    }
    return count;
}

static ssize_t zxrio_write_at(fdio_t* io, const void* data, size_t len, off_t offset) {
    zxrio_t* rio = (zxrio_t*) io;
    zx_status_t status = ZX_OK;
    uint64_t count = 0;
    uint64_t xfer;
    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;
        uint64_t actual = 0;
        if ((status = fidl_writeat(rio, data, xfer, offset, &actual)) != ZX_OK) {
            return status;
        }
        count += actual;
        data += actual;
        offset += actual;
        len -= actual;
        if (xfer != actual) {
            break;
        }
    }
    if (count == 0) {
        return status;
    }
    return count;
}

static zx_status_t zxrio_get_attr(fdio_t* io, vnattr_t* out) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_stat(rio, out);
}

static ssize_t zxrio_read(fdio_t* io, void* data, size_t len) {
    zxrio_t* rio = (zxrio_t*) io;
    zx_status_t status = ZX_OK;
    uint64_t count = 0;
    uint64_t xfer;
    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;
        uint64_t actual = 0;
        if ((status = fidl_read(rio, data, xfer, &actual)) != ZX_OK) {
            return status;
        }
        count += actual;
        data += actual;
        len -= actual;
        if (xfer != actual) {
            break;
        }
    }
    if (count == 0) {
        return status;
    }
    return count;
}

static ssize_t zxrio_read_at(fdio_t* io, void* data, size_t len, off_t offset) {
    zxrio_t* rio = (zxrio_t*) io;
    zx_status_t status = ZX_OK;
    uint64_t count = 0;
    uint64_t xfer;
    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;
        uint64_t actual = 0;
        if ((status = fidl_readat(rio, data, xfer, offset, &actual)) != ZX_OK) {
            return status;
        }
        offset += actual;
        count += actual;
        data += actual;
        len -= actual;
        if (xfer != actual) {
            break;
        }
    }
    if (count == 0) {
        return status;
    }
    return count;
}

static off_t zxrio_seek(fdio_t* io, off_t offset, int whence) {
    zxrio_t* rio = (zxrio_t*)io;
    zx_status_t status = fidl_seek(rio, offset, whence, &offset);
    if (status != ZX_OK) {
        return status;
    }
    return offset;
}

static ssize_t zxrio_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                    size_t in_len, void* out_buf, size_t out_len) {
    zxrio_t* rio = (zxrio_t*)io;
    if (in_len > FDIO_IOCTL_MAX_INPUT || out_len > FDIO_CHUNK_SIZE) {
        return ZX_ERR_INVALID_ARGS;
    }
    size_t actual;
    zx_status_t status = fidl_ioctl(rio, op, in_buf, in_len, out_buf, out_len, &actual);
    if (status != ZX_OK) {
        return status;
    }
    return actual;
}

// Takes ownership of the optional |extra_handle|.
//
// Decodes the handle into |info|, if it exists and should
// be decoded.
static zx_status_t zxrio_decode_describe_handle(zxrio_describe_t* info,
                                                zx_handle_t extra_handle) {
    bool have_handle = (extra_handle != ZX_HANDLE_INVALID);
    bool want_handle = false;
    zx_handle_t* handle_target = NULL;

    switch (info->extra.tag) {
    // Case: No extra handles expected
    case fuchsia_io_NodeInfoTag_service:
    case fuchsia_io_NodeInfoTag_directory:
        break;
    // Case: Extra handles optional
    case fuchsia_io_NodeInfoTag_file:
        handle_target = &info->extra.file.e;
        goto handle_optional;
    case fuchsia_io_NodeInfoTag_device:
        handle_target = &info->extra.device.e;
        goto handle_optional;
handle_optional:
        want_handle = *handle_target == FIDL_HANDLE_PRESENT;
        break;
    // Case: Extra handles required
    case fuchsia_io_NodeInfoTag_pipe:
        handle_target = &info->extra.pipe.s;
        goto handle_required;
    case fuchsia_io_NodeInfoTag_vmofile:
        handle_target = &info->extra.vmofile.v;
        goto handle_required;
handle_required:
        want_handle = *handle_target == FIDL_HANDLE_PRESENT;
        if (!want_handle) {
            goto fail;
        }
        break;
    default:
        printf("Unexpected protocol type opening connection\n");
        goto fail;
    }

    if (have_handle != want_handle) {
        goto fail;
    }
    if (have_handle) {
        *handle_target = extra_handle;
    }
    return ZX_OK;

fail:
    if (have_handle) {
        zx_handle_close(extra_handle);
    }
    return ZX_ERR_IO;
}

// Wait/Read from a new client connection, with the expectation of
// acquiring an Open response.
//
// Shared implementation between RemoteIO and FIDL, since the response
// message is aligned.
//
// Does not close |h|, even on error.
static zx_status_t zxrio_process_open_response(zx_handle_t h, zxrio_describe_t* info) {
    zx_object_wait_one(h, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                       ZX_TIME_INFINITE, NULL);

    // Attempt to read the description from open
    uint32_t dsize = sizeof(*info);
    zx_handle_t extra_handle = ZX_HANDLE_INVALID;
    uint32_t actual_handles;
    zx_status_t r = zx_channel_read(h, 0, info, &extra_handle, dsize, 1, &dsize,
                                    &actual_handles);
    if (r != ZX_OK) {
        return r;
    }
    if (dsize < ZXRIO_DESCRIBE_HDR_SZ || info->hdr.ordinal != fuchsia_io_NodeOnOpenOrdinal) {
        r = ZX_ERR_IO;
    } else {
        r = info->status;
    }

    if (dsize != sizeof(zxrio_describe_t)) {
        r = (r != ZX_OK) ? r : ZX_ERR_IO;
    }

    if (r != ZX_OK) {
        if (extra_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(extra_handle);
        }
        return r;
    }

    // Confirm that the objects "zxrio_describe_t" and "fuchsia_io_NodeOnOpenEvent"
    // are aligned enough to be compatible.
    //
    // This is somewhat complicated by the fact that the "fuchsia_io_NodeOnOpenEvent"
    // object has an optional "fuchsia_io_NodeInfo" secondary which exists immediately
    // following the struct.
    static_assert(__builtin_offsetof(zxrio_describe_t, extra) ==
                  FIDL_ALIGN(sizeof(fuchsia_io_NodeOnOpenEvent)),
                  "RIO Description message doesn't align with FIDL response secondary");
    static_assert(sizeof(zxrio_node_info_t) == sizeof(fuchsia_io_NodeInfo),
                  "RIO Node Info doesn't align with FIDL object info");
    static_assert(__builtin_offsetof(zxrio_node_info_t, file.e) ==
                  __builtin_offsetof(fuchsia_io_NodeInfo, file.event), "Unaligned File");
    static_assert(__builtin_offsetof(zxrio_node_info_t, pipe.s) ==
                  __builtin_offsetof(fuchsia_io_NodeInfo, pipe.socket), "Unaligned Pipe");
    static_assert(__builtin_offsetof(zxrio_node_info_t, vmofile.v) ==
                  __builtin_offsetof(fuchsia_io_NodeInfo, vmofile.vmo), "Unaligned Vmofile");
    static_assert(__builtin_offsetof(zxrio_node_info_t, device.e) ==
                  __builtin_offsetof(fuchsia_io_NodeInfo, device.event), "Unaligned Device");
    // Connection::NodeDescribe also relies on these static_asserts.
    // fidl_describe also relies on these static_asserts.

    return zxrio_decode_describe_handle(info, extra_handle);
}

__EXPORT
zx_status_t fdio_service_connect(const char* svcpath, zx_handle_t h) {
    if (svcpath == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    // Otherwise attempt to connect through the root namespace
    if (fdio_root_ns != NULL) {
        return fdio_ns_connect(fdio_root_ns, svcpath,
                               ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, h);
    }
    // Otherwise we fail
    zx_handle_close(h);
    return ZX_ERR_NOT_FOUND;
}

__EXPORT
zx_status_t fdio_service_connect_at(zx_handle_t dir, const char* path, zx_handle_t h) {
    if (path == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    if (dir == ZX_HANDLE_INVALID) {
        zx_handle_close(h);
        return ZX_ERR_UNAVAILABLE;
    }
    return zxrio_connect(dir, h, fuchsia_io_DirectoryOpenOrdinal, ZX_FS_RIGHT_READABLE |
                         ZX_FS_RIGHT_WRITABLE, 0755, path);
}

__EXPORT
zx_status_t fdio_open_at(zx_handle_t dir, const char* path, uint32_t flags, zx_handle_t h) {
    if (path == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    if (dir == ZX_HANDLE_INVALID) {
        zx_handle_close(h);
        return ZX_ERR_UNAVAILABLE;
    }
    return zxrio_connect(dir, h, fuchsia_io_DirectoryOpenOrdinal, flags, 0755, path);
}

__EXPORT
zx_handle_t fdio_service_clone(zx_handle_t svc) {
    zx_handle_t cli, srv;
    zx_status_t r;
    if (svc == ZX_HANDLE_INVALID) {
        return ZX_HANDLE_INVALID;
    }
    if ((r = zx_channel_create(0, &cli, &srv)) < 0) {
        return ZX_HANDLE_INVALID;
    }
    if ((r = zxrio_connect(svc, srv, fuchsia_io_NodeCloneOrdinal, ZX_FS_RIGHT_READABLE |
                           ZX_FS_RIGHT_WRITABLE, 0755, "")) < 0) {
        zx_handle_close(cli);
        return ZX_HANDLE_INVALID;
    }
    return cli;
}

__EXPORT
zx_status_t fdio_service_clone_to(zx_handle_t svc, zx_handle_t srv) {
    if (srv == ZX_HANDLE_INVALID) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (svc == ZX_HANDLE_INVALID) {
        zx_handle_close(srv);
        return ZX_ERR_INVALID_ARGS;
    }
    return zxrio_connect(svc, srv, fuchsia_io_NodeCloneOrdinal, ZX_FS_RIGHT_READABLE |
                         ZX_FS_RIGHT_WRITABLE, 0755, "");
}

zx_status_t fdio_acquire_socket(zx_handle_t socket, fdio_t** out_io) {
    zx_info_socket_t info;
    memset(&info, 0, sizeof(info));
    zx_status_t status = zx_object_get_info(socket, ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
        zx_handle_close(socket);
        return status;
    }
    fdio_t* io = NULL;
    if ((info.options & ZX_SOCKET_HAS_CONTROL) != 0) {
        // If the socket has a control plane, then the socket is either
        // a stream or a datagram socket.
        if ((info.options & ZX_SOCKET_DATAGRAM) != 0) {
            io = fdio_socket_create_datagram(socket, IOFLAG_SOCKET_CONNECTED);
        } else {
            io = fdio_socket_create_stream(socket, IOFLAG_SOCKET_CONNECTED);
        }
    } else {
        // Without a control plane, the socket is a pipe.
        io = fdio_pipe_create(socket);
    }
    if (!io) {
        return ZX_ERR_NO_RESOURCES;
    }
    *out_io = io;
    return ZX_OK;
}

// Create a fdio (if possible) from handles and info.
//
// The Control channel is provided in |handle|, and auxillary
// handles may be provided in the |info| object.
//
// This function always takes control of all handles.
// They are transferred into the |out| object on success,
// or closed on failure.
static zx_status_t fdio_from_handles(zx_handle_t handle, zxrio_node_info_t* info,
                                     fdio_t** out) {
    // All failure cases which discard handles set r and break
    // to the end. All other cases in which handle ownership is moved
    // on return locally.
    zx_status_t r;
    fdio_t* io;
    switch (info->tag) {
    case fuchsia_io_NodeInfoTag_directory:
    case fuchsia_io_NodeInfoTag_service:
        if (handle == ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        }
        io = fdio_remote_create(handle, 0);
        xprintf("rio (%x,%x) -> %p\n", handle, 0, io);
        if (io == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        *out = io;
        return ZX_OK;
    case fuchsia_io_NodeInfoTag_file:
        if (info->file.e == ZX_HANDLE_INVALID) {
            io = fdio_remote_create(handle, 0);
            xprintf("rio (%x,%x) -> %p\n", handle, 0, io);
        } else {
            io = fdio_remote_create(handle, info->file.e);
            xprintf("rio (%x,%x) -> %p\n", handle, info->file.e, io);
        }
        if (io == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        *out = io;
        return ZX_OK;
    case fuchsia_io_NodeInfoTag_device:
        if (info->device.e == ZX_HANDLE_INVALID) {
            io = fdio_remote_create(handle, 0);
            xprintf("rio (%x,%x) -> %p\n", handle, 0, io);
        } else {
            io = fdio_remote_create(handle, info->device.e);
            xprintf("rio (%x,%x) -> %p\n", handle, info->device.e, io);
        }
        if (io == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        *out = io;
        return ZX_OK;
    case fuchsia_io_NodeInfoTag_vmofile: {
        if (info->vmofile.v == ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        }
        *out = fdio_vmofile_create(handle, info->vmofile.v, info->vmofile.offset,
                                   info->vmofile.length);
        if (*out == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        return ZX_OK;
    }
    case fuchsia_io_NodeInfoTag_pipe: {
        if (info->pipe.s == ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        }
        zx_handle_close(handle);
        return fdio_acquire_socket(info->pipe.s, out);
    }
    default:
        printf("fdio_from_handles: Not supported\n");
        r = ZX_ERR_NOT_SUPPORTED;
        break;
    }
    zx_handle_t extra;
    if (zxrio_object_extract_handle(info, &extra) == ZX_OK) {
        zx_handle_close(extra);
    }
    zx_handle_close(handle);
    return r;
}

__EXPORT
zx_status_t fdio_create_fd(zx_handle_t* handles, uint32_t* types, size_t hcount,
                           int* fd_out) {
    fdio_t* io;
    zx_status_t r;
    int fd;
    zxrio_node_info_t info;

    // Pack additional handles into |info|, if possible.
    switch (PA_HND_TYPE(types[0])) {
    case PA_FDIO_REMOTE:
        switch (hcount) {
        case 1:
            io = fdio_remote_create(handles[0], 0);
            goto bind;
        case 2:
            io = fdio_remote_create(handles[0], handles[1]);
            goto bind;
        default:
            r = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
    case PA_FDIO_SOCKET:
        info.tag = fuchsia_io_NodeInfoTag_pipe;
        // Expected: Single socket handle
        if (hcount != 1) {
            r = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
        info.pipe.s = handles[0];
        break;
    default:
        r = ZX_ERR_IO;
        goto fail;
    }

    if ((r = fdio_from_handles(ZX_HANDLE_INVALID, &info, &io)) != ZX_OK) {
        return r;
    }

bind:
    fd = fdio_bind_to_fd(io, -1, 0);
    if (fd < 0) {
        fdio_close(io);
        fdio_release(io);
        return ZX_ERR_BAD_STATE;
    }

    *fd_out = fd;
    return ZX_OK;
fail:
    zx_handle_close_many(handles, hcount);
    return r;
}

// Synchronously (non-pipelined) open an object
// The svc handle is only used to send a message
static zx_status_t zxrio_sync_open_connection(zx_handle_t svc, uint32_t op,
                                              uint32_t flags, uint32_t mode,
                                              const char* path, size_t pathlen,
                                              zxrio_describe_t* info, zx_handle_t* out) {
    if (!(flags & ZX_FS_FLAG_DESCRIBE)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t r;
    zx_handle_t h;
    zx_handle_t cnxn;
    if ((r = zx_channel_create(0, &h, &cnxn)) != ZX_OK) {
        return r;
    }

    switch (op) {
    case fuchsia_io_NodeCloneOrdinal:
        r = fidl_clone_request(svc, cnxn, flags);
        break;
    case fuchsia_io_DirectoryOpenOrdinal:
        r = fidl_open_request(svc, cnxn, flags, mode, path, pathlen);
        break;
    default:
        zx_handle_close(cnxn);
        r = ZX_ERR_NOT_SUPPORTED;
    }

    if (r != ZX_OK) {
        zx_handle_close(h);
        return r;
    }

    if ((r = zxrio_process_open_response(h, info)) != ZX_OK) {
        zx_handle_close(h);
        return r;
    }
    *out = h;
    return ZX_OK;
}

// Acquires a new connection to an object.
//
// Returns a description of the opened object in |info|, and
// the control channel to the object in |out|.
//
// |info| may contain an additional handle.
static zx_status_t zxrio_getobject(zx_handle_t rio_h, uint32_t op, const char* name,
                                   uint32_t flags, uint32_t mode,
                                   zxrio_describe_t* info, zx_handle_t* out) {
    if (name == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t len = strlen(name);
    if (len >= PATH_MAX) {
        return ZX_ERR_BAD_PATH;
    }

    if (flags & ZX_FS_FLAG_DESCRIBE) {
        return zxrio_sync_open_connection(rio_h, op, flags, mode, name, len, info, out);
    } else {
        zx_handle_t h0, h1;
        zx_status_t r;
        if ((r = zx_channel_create(0, &h0, &h1)) < 0) {
            return r;
        }
        if ((r = zxrio_connect(rio_h, h1, op, flags, mode, name)) < 0) {
            zx_handle_close(h0);
            return r;
        }
        // fake up a reply message since pipelined opens don't generate one
        info->status = ZX_OK;
        info->extra.tag = fuchsia_io_NodeInfoTag_service;
        *out = h0;
        return ZX_OK;
    }
}

zx_status_t zxrio_open_handle(zx_handle_t h, const char* path, uint32_t flags,
                              uint32_t mode, fdio_t** out) {
    zx_handle_t control_channel;
    zxrio_describe_t info;
    zx_status_t r = zxrio_getobject(h, fuchsia_io_DirectoryOpenOrdinal, path, flags, mode, &info, &control_channel);
    if (r < 0) {
        return r;
    }
    return fdio_from_handles(control_channel, &info.extra, out);
}

static zx_status_t zxrio_open(fdio_t* io, const char* path, uint32_t flags, uint32_t mode, fdio_t** out) {
    zxrio_t* rio = (void*)io;
    return zxrio_open_handle(rio->h, path, flags, mode, out);
}

static zx_status_t zxrio_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zxrio_t* rio = (void*)io;
    zx_handle_t h;
    zxrio_describe_t info;
    zx_status_t r = zxrio_getobject(rio->h, fuchsia_io_NodeCloneOrdinal, "", 0, 0, &info, &h);
    if (r < 0) {
        return r;
    }
    handles[0] = h;
    types[0] = PA_FDIO_REMOTE;
    return 1;
}

static zx_status_t zxrio_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zxrio_t* rio = (void*)io;
    LOG(1, "fdio: zxrio_unwrap(%p,...)\n");
    handles[0] = rio->h;
    types[0] = PA_FDIO_REMOTE;
    if (rio->event != ZX_HANDLE_INVALID) {
        zx_handle_close(rio->event);
        rio->event = ZX_HANDLE_INVALID;
    }
    return 1;
}

static void zxrio_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zxrio_t* rio = (void*)io;
    *handle = rio->event;

    zx_signals_t signals = 0;
    // Manually add signals that don't fit within POLL_MASK
    if (events & POLLRDHUP) {
        signals |= ZX_CHANNEL_PEER_CLOSED;
    }

    // POLLERR is always detected
    *_signals = (((POLLERR | events) & POLL_MASK) << POLL_SHIFT) | signals;
}

static void zxrio_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
    // Manually add events that don't fit within POLL_MASK
    uint32_t events = 0;
    if (signals & ZX_CHANNEL_PEER_CLOSED) {
        events |= POLLRDHUP;
    }
    *_events = ((signals >> POLL_SHIFT) & POLL_MASK) | events;
}

static zx_status_t zxrio_get_vmo(fdio_t* io, int flags, zx_handle_t* out) {
    zx_handle_t vmo;
    zxrio_t* rio = (zxrio_t*)io;
    zx_status_t r = fidl_getvmo(rio, flags, &vmo);
    if (r != ZX_OK) {
        return r;
    }
    *out = vmo;
    return ZX_OK;
}

static zx_status_t zxrio_get_token(fdio_t* io, zx_handle_t* out) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_gettoken(rio, out);
}

static zx_status_t zxrio_set_attr(fdio_t* io, const vnattr_t* attr) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_setattr(rio, attr);
}

static zx_status_t zxrio_sync(fdio_t* io) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_sync(rio);
}

static zx_status_t zxrio_readdir(fdio_t* io, void* ptr, size_t max, size_t* actual) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_readdirents(rio, ptr, max, actual);
}

static zx_status_t zxrio_rewind(fdio_t* io) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_rewind(rio);
}

static zx_status_t zxrio_unlink(fdio_t* io, const char* path, size_t len) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_unlink(rio, path, len);
}

static zx_status_t zxrio_truncate(fdio_t* io, off_t off) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_truncate(rio, off);
}

static zx_status_t zxrio_rename(fdio_t* io, const char* src, size_t srclen,
                                zx_handle_t dst_token, const char* dst, size_t dstlen) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_rename(rio, src, srclen, dst_token, dst, dstlen);
}

static zx_status_t zxrio_link(fdio_t* io, const char* src, size_t srclen,
                              zx_handle_t dst_token, const char* dst, size_t dstlen) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_link(rio, src, srclen, dst_token, dst, dstlen);
}

static zx_status_t zxrio_get_flags(fdio_t* io, uint32_t* out_flags) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_getflags(rio, out_flags);
}

static zx_status_t zxrio_set_flags(fdio_t* io, uint32_t flags) {
    zxrio_t* rio = (zxrio_t*)io;
    return fidl_setflags(rio, flags);
}

fdio_ops_t zx_remote_ops = {
    .read = zxrio_read,
    .read_at = zxrio_read_at,
    .write = zxrio_write,
    .write_at = zxrio_write_at,
    .seek = zxrio_seek,
    .misc = fdio_default_misc,
    .close = zxrio_close,
    .open = zxrio_open,
    .clone = zxrio_clone,
    .ioctl = zxrio_ioctl,
    .wait_begin = zxrio_wait_begin,
    .wait_end = zxrio_wait_end,
    .unwrap = zxrio_unwrap,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = zxrio_get_vmo,
    .get_token = zxrio_get_token,
    .get_attr = zxrio_get_attr,
    .set_attr = zxrio_set_attr,
    .sync = zxrio_sync,
    .readdir = zxrio_readdir,
    .rewind = zxrio_rewind,
    .unlink = zxrio_unlink,
    .truncate = zxrio_truncate,
    .rename = zxrio_rename,
    .link = zxrio_link,
    .get_flags = zxrio_get_flags,
    .set_flags = zxrio_set_flags,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .shutdown = fdio_default_shutdown,
};

__EXPORT
fdio_t* fdio_remote_create(zx_handle_t h, zx_handle_t event) {
    zxrio_t* rio = fdio_alloc(sizeof(*rio));
    if (rio == NULL) {
        zx_handle_close(h);
        zx_handle_close(event);
        return NULL;
    }
    rio->io.ops = &zx_remote_ops;
    rio->io.magic = FDIO_MAGIC;
    atomic_init(&rio->io.refcount, 1);
    rio->h = h;
    rio->event = event;
    return &rio->io;
}
