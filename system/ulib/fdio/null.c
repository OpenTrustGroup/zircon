// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/fdio/io.h>

#include "private.h"

zx_status_t fdio_default_get_token(fdio_t* io, zx_handle_t* out) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_get_attr(fdio_t* io, vnattr_t* out) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_set_attr(fdio_t* io, const vnattr_t* attr) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_sync(fdio_t* io) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_readdir(fdio_t* io, void* ptr, size_t max, size_t* actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_rewind(fdio_t* io) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_unlink(fdio_t* io, const char* path, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_truncate(fdio_t* io, off_t off) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_rename(fdio_t* io, const char* src, size_t srclen,
                                zx_handle_t dst_token, const char* dst, size_t dstlen) {
    zx_handle_close(dst_token);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_link(fdio_t* io, const char* src, size_t srclen,
                              zx_handle_t dst_token, const char* dst, size_t dstlen) {
    zx_handle_close(dst_token);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_get_flags(fdio_t* io, uint32_t* out_flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_set_flags(fdio_t* io, uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

ssize_t fdio_default_read(fdio_t* io, void* _data, size_t len) {
    return 0;
}

ssize_t fdio_default_read_at(fdio_t* io, void* _data, size_t len, off_t offset) {
    return ZX_ERR_WRONG_TYPE;
}

ssize_t fdio_default_write(fdio_t* io, const void* _data, size_t len) {
    return len;
}

ssize_t fdio_default_write_at(fdio_t* io, const void* _data, size_t len, off_t offset) {
    return ZX_ERR_WRONG_TYPE;
}

ssize_t fdio_default_recvfrom(fdio_t* io, void* data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    return ZX_ERR_WRONG_TYPE;
}

ssize_t fdio_default_sendto(fdio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    return ZX_ERR_WRONG_TYPE;
}

ssize_t fdio_default_recvmsg(fdio_t* io, struct msghdr* msg, int flags) {
    return ZX_ERR_WRONG_TYPE;
}

ssize_t fdio_default_sendmsg(fdio_t* io, const struct msghdr* msg, int flags) {
    return ZX_ERR_WRONG_TYPE;
}

off_t fdio_default_seek(fdio_t* io, off_t offset, int whence) {
    return ZX_ERR_WRONG_TYPE;
}

zx_status_t fdio_default_misc(fdio_t* io, uint32_t op, int64_t off, uint32_t arg, void* data, size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_open(fdio_t* io, const char* path, uint32_t flags, uint32_t mode, fdio_t** out) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_shutdown(fdio_t* io, int how) {
    return ZX_ERR_WRONG_TYPE;
}

zx_status_t fdio_default_close(fdio_t* io) {
    return ZX_OK;
}

ssize_t fdio_default_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

void fdio_default_wait_begin(fdio_t* io, uint32_t events,
                             zx_handle_t* handle, zx_signals_t* _signals) {
    *handle = ZX_HANDLE_INVALID;
}

void fdio_default_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
}

ssize_t fdio_default_posix_ioctl(fdio_t* io, int req, va_list va) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_get_vmo(fdio_t* io, int flags, zx_handle_t* out) {
    return ZX_ERR_NOT_SUPPORTED;
}

static fdio_ops_t zx_null_ops = {
    .read = fdio_default_read,
    .read_at = fdio_default_read_at,
    .write = fdio_default_write,
    .write_at = fdio_default_write_at,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = fdio_default_close,
    .open = fdio_default_open,
    .clone = fdio_default_clone,
    .ioctl = fdio_default_ioctl,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .unwrap = fdio_default_unwrap,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
    .sync = fdio_default_sync,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .shutdown = fdio_default_shutdown,
};

__EXPORT
fdio_t* fdio_null_create(void) {
    fdio_t* io = fdio_alloc(sizeof(*io));
    if (io == NULL) {
        return NULL;
    }
    io->ops = &zx_null_ops;
    io->magic = FDIO_MAGIC;
    atomic_init(&io->refcount, 1);
    return io;
}
