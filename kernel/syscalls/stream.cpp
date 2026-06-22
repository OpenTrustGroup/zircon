// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <trace.h>

#include <lib/user_copy/user_ptr.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/stream_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include "priv.h"

#define LOCAL_TRACE 0

zx_status_t sys_stream_create(uint32_t options, zx_handle_t vmo_handle, zx_off_t seek,
                              user_out_handle* out_stream) {
    if ((options & ~ZX_STREAM_CREATE_MASK) != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_rights_t vmo_rights = ZX_RIGHT_NONE;
    if (options & ZX_STREAM_MODE_READ) {
        vmo_rights |= ZX_RIGHT_READ;
    }
    if (options & ZX_STREAM_MODE_WRITE) {
        vmo_rights |= ZX_RIGHT_WRITE;
    }

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_status_t status = up->GetDispatcherWithRights(vmo_handle, vmo_rights, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<Dispatcher> stream;
    zx_rights_t rights = 0u;
    status = StreamDispatcher::Create(options, fbl::move(vmo), seek, &stream, &rights);
    if (status != ZX_OK) {
        return status;
    }
    return out_stream->make(fbl::move(stream), rights);
}

zx_status_t sys_stream_writev(zx_handle_t handle, uint32_t options,
                              user_in_ptr<const zx_iovec_t> vector, size_t vector_count,
                              user_out_ptr<size_t> out_actual) {
    LTRACEF("handle %x\n", handle);

    if (options & ~ZX_STREAM_APPEND) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!vector && vector_count > 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<StreamDispatcher> stream;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &stream);
    if (status != ZX_OK) {
        return status;
    }

    size_t actual = 0;
    if (options & ZX_STREAM_APPEND) {
        status = stream->AppendVector(vector, vector_count, &actual);
    } else {
        status = stream->WriteVector(vector_count, vector, &actual);
    }

    if (status == ZX_OK && out_actual) {
        status = out_actual.copy_to_user(actual);
    }
    return status;
}

zx_status_t sys_stream_writev_at(zx_handle_t handle, uint32_t options, zx_off_t offset,
                                 user_in_ptr<const zx_iovec_t> vector, size_t vector_count,
                                 user_out_ptr<size_t> out_actual) {
    LTRACEF("handle %x\n", handle);

    if (options != 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!vector && vector_count > 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<StreamDispatcher> stream;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &stream);
    if (status != ZX_OK) {
        return status;
    }

    size_t actual = 0;
    status = stream->WriteVector(vector, vector_count, &offset, &actual);
    if (status == ZX_OK && out_actual) {
        status = out_actual.copy_to_user(actual);
    }
    return status;
}

zx_status_t sys_stream_readv(zx_handle_t handle, uint32_t options,
                             user_in_ptr<const zx_iovec_t> vector, size_t vector_count,
                             user_out_ptr<size_t> out_actual) {
    LTRACEF("handle %x\n", handle);

    if (options != 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!vector && vector_count > 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<StreamDispatcher> stream;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &stream);
    if (status != ZX_OK) {
        return status;
    }

    size_t actual = 0;
    status = stream->ReadVector(vector_count, vector, &actual);

    if (status == ZX_OK && out_actual) {
        status = out_actual.copy_to_user(actual);
    }
    return status;
}

zx_status_t sys_stream_readv_at(zx_handle_t handle, uint32_t options, zx_off_t offset,
                                user_in_ptr<const zx_iovec_t> vector, size_t vector_count,
                                user_out_ptr<size_t> out_actual) {
    LTRACEF("handle %x\n", handle);

    if (options != 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!vector && vector_count > 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<StreamDispatcher> stream;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &stream);
    if (status != ZX_OK) {
        return status;
    }

    size_t actual = 0;
    status = stream->ReadVector(vector, vector_count, &offset, &actual);
    if (status == ZX_OK && out_actual) {
        status = out_actual.copy_to_user(actual);
    }
    return status;
}

zx_status_t sys_stream_seek(zx_handle_t handle, zx_stream_seek_origin_t whence, int64_t offset,
                            user_out_ptr<zx_off_t> out_seek) {
    LTRACEF("handle %x\n", handle);

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<StreamDispatcher> stream;
    zx_rights_t rights = 0u;
    zx_status_t status = up->GetDispatcherAndRights(handle, &stream, &rights);
    if (status != ZX_OK) {
        return status;
    }
    if ((rights & (ZX_RIGHT_READ | ZX_RIGHT_WRITE)) == 0) {
        return ZX_ERR_ACCESS_DENIED;
    }

    zx_off_t seek = 0u;
    status = stream->Seek(whence, offset, &seek);
    if (status == ZX_OK && out_seek) {
        status = out_seek.copy_to_user(seek);
    }
    return status;
}
