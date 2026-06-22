// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/stream_dispatcher.h>

#include <lib/counters.h>
#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_stream_create_count, "dispatcher.stream.create");
KCOUNTER(dispatcher_stream_destroy_count, "dispatcher.stream.destroy");

zx_status_t StreamDispatcher::Create(uint32_t options, fbl::RefPtr<VmObjectDispatcher> vmo,
                                     zx_off_t seek, fbl::RefPtr<Dispatcher>* dispatcher,
                                     zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) StreamDispatcher(options, fbl::move(vmo), seek);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_rights_t new_rights = ZX_DEFAULT_STREAM_RIGHTS;
    if (options & ZX_STREAM_MODE_READ) {
        new_rights |= ZX_RIGHT_READ;
    }
    if (options & ZX_STREAM_MODE_WRITE) {
        new_rights |= ZX_RIGHT_WRITE;
    }

    *rights = new_rights;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

StreamDispatcher::StreamDispatcher(uint32_t options, fbl::RefPtr<VmObjectDispatcher> vmo,
                                   zx_off_t seek)
    : options_(options), vmo_(fbl::move(vmo)), seek_(seek) {
    kcounter_add(dispatcher_stream_create_count, 1);
}

StreamDispatcher::~StreamDispatcher() {
    kcounter_add(dispatcher_stream_destroy_count, 1);
}

zx_status_t StreamDispatcher::UserIovecTotal(user_in_ptr<const zx_iovec_t> user_data,
                                             size_t vector_count,
                                             size_t* total_capacity) const {
    size_t total = 0;
    for (size_t i = 0; i < vector_count; ++i) {
        zx_iovec_t current = {};
        zx_status_t status = user_data.element_offset(i).copy_from_user(&current);
        if (status != ZX_OK) {
            return status;
        }
        if (current.capacity > 0 && current.buffer == nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (add_overflow(total, current.capacity, &total)) {
            return ZX_ERR_INVALID_ARGS;
        }
    }
    *total_capacity = total;
    return ZX_OK;
}

zx_status_t StreamDispatcher::ReadVectorAt(user_in_ptr<const zx_iovec_t> user_data,
                                           size_t vector_count, zx_off_t offset, size_t length) {
    size_t done = 0;
    for (size_t i = 0; i < vector_count && done < length; ++i) {
        zx_iovec_t current = {};
        zx_status_t status = user_data.element_offset(i).copy_from_user(&current);
        if (status != ZX_OK) {
            return status;
        }
        size_t copy_len = MIN(current.capacity, length - done);
        if (copy_len == 0) {
            continue;
        }
        status = vmo_->Read(user_out_ptr<void>(current.buffer), copy_len, offset + done);
        if (status != ZX_OK) {
            return status;
        }
        done += copy_len;
    }
    return ZX_OK;
}

zx_status_t StreamDispatcher::WriteVectorAt(user_in_ptr<const zx_iovec_t> user_data,
                                            size_t vector_count, zx_off_t offset, size_t length) {
    size_t done = 0;
    for (size_t i = 0; i < vector_count && done < length; ++i) {
        zx_iovec_t current = {};
        zx_status_t status = user_data.element_offset(i).copy_from_user(&current);
        if (status != ZX_OK) {
            return status;
        }
        size_t copy_len = MIN(current.capacity, length - done);
        if (copy_len == 0) {
            continue;
        }
        status = vmo_->Write(user_in_ptr<const void>(current.buffer), copy_len, offset + done);
        if (status != ZX_OK) {
            return status;
        }
        done += copy_len;
    }
    return ZX_OK;
}

zx_status_t StreamDispatcher::ReadVector(user_in_ptr<const zx_iovec_t> user_data,
                                         size_t vector_count, zx_off_t* offset,
                                         size_t* out_actual) {
    canary_.Assert();

    size_t total_capacity = 0;
    zx_status_t status = UserIovecTotal(user_data, vector_count, &total_capacity);
    if (status != ZX_OK) {
        return status;
    }

    uint64_t content_size = vmo_->GetContentSize();
    if (*offset >= content_size) {
        *out_actual = 0u;
        return ZX_OK;
    }

    size_t length = MIN(total_capacity, content_size - *offset);
    status = ReadVectorAt(user_data, vector_count, *offset, length);
    if (status != ZX_OK) {
        return status;
    }
    *offset += length;
    *out_actual = length;
    return ZX_OK;
}

zx_status_t StreamDispatcher::ReadVector(size_t vector_count, user_in_ptr<const zx_iovec_t> user_data,
                                         size_t* out_actual) {
    canary_.Assert();

    zx_off_t offset = 0;
    {
        fbl::AutoLock lock(get_lock());
        offset = seek_;
    }

    zx_status_t status = ReadVector(user_data, vector_count, &offset, out_actual);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AutoLock lock(get_lock());
    seek_ = offset;
    return ZX_OK;
}

zx_status_t StreamDispatcher::WriteVector(user_in_ptr<const zx_iovec_t> user_data,
                                          size_t vector_count, zx_off_t* offset,
                                          size_t* out_actual) {
    canary_.Assert();

    size_t total_capacity = 0;
    zx_status_t status = UserIovecTotal(user_data, vector_count, &total_capacity);
    if (status != ZX_OK) {
        return status;
    }

    uint64_t requested_content_size = 0u;
    if (add_overflow(*offset, total_capacity, &requested_content_size)) {
        return ZX_ERR_FILE_BIG;
    }

    uint64_t content_size = vmo_->ExpandContentIfNeeded(requested_content_size, *offset);
    if (*offset >= content_size) {
        return ZX_ERR_NO_SPACE;
    }

    size_t length = MIN(total_capacity, content_size - *offset);
    status = WriteVectorAt(user_data, vector_count, *offset, length);
    if (status != ZX_OK) {
        return status;
    }
    *offset += length;
    *out_actual = length;
    return ZX_OK;
}

zx_status_t StreamDispatcher::WriteVector(size_t vector_count,
                                          user_in_ptr<const zx_iovec_t> user_data,
                                          size_t* out_actual) {
    canary_.Assert();

    zx_off_t offset = 0;
    {
        fbl::AutoLock lock(get_lock());
        offset = seek_;
    }

    zx_status_t status = WriteVector(user_data, vector_count, &offset, out_actual);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AutoLock lock(get_lock());
    seek_ = offset;
    return ZX_OK;
}

zx_status_t StreamDispatcher::AppendVector(user_in_ptr<const zx_iovec_t> user_data,
                                           size_t vector_count, size_t* out_actual) {
    canary_.Assert();

    zx_off_t offset = vmo_->GetContentSize();
    zx_status_t status = WriteVector(user_data, vector_count, &offset, out_actual);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AutoLock lock(get_lock());
    seek_ = offset;
    return ZX_OK;
}

zx_status_t StreamDispatcher::Seek(zx_stream_seek_origin_t whence, int64_t offset,
                                   zx_off_t* out_seek) {
    canary_.Assert();

    fbl::AutoLock lock(get_lock());

    zx_off_t target = 0;
    switch (whence) {
        case ZX_STREAM_SEEK_ORIGIN_START:
            if (offset < 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            target = static_cast<zx_off_t>(offset);
            break;
        case ZX_STREAM_SEEK_ORIGIN_CURRENT:
            if (add_overflow(seek_, offset, &target)) {
                return ZX_ERR_INVALID_ARGS;
            }
            break;
        case ZX_STREAM_SEEK_ORIGIN_END:
            if (add_overflow(vmo_->GetContentSize(), offset, &target)) {
                return ZX_ERR_INVALID_ARGS;
            }
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
    }

    seek_ = target;
    *out_seek = seek_;
    return ZX_OK;
}

void StreamDispatcher::GetInfo(zx_info_stream_t* info) const {
    canary_.Assert();

    fbl::AutoLock lock(get_lock());
    info->options = options_;
    info->seek = seek_;
    info->content_size = vmo_->GetContentSize();
}
