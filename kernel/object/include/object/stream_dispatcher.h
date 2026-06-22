// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/user_copy/user_ptr.h>
#include <object/dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <zircon/types.h>

class StreamDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(uint32_t options, fbl::RefPtr<VmObjectDispatcher> vmo,
                              zx_off_t seek, fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);
    ~StreamDispatcher() final;

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_STREAM; }

    zx_status_t ReadVector(user_in_ptr<const zx_iovec_t> user_data, size_t vector_count,
                           zx_off_t* offset, size_t* out_actual);
    zx_status_t WriteVector(user_in_ptr<const zx_iovec_t> user_data, size_t vector_count,
                            zx_off_t* offset, size_t* out_actual);
    zx_status_t AppendVector(user_in_ptr<const zx_iovec_t> user_data, size_t vector_count,
                             size_t* out_actual);
    zx_status_t ReadVector(size_t vector_count, user_in_ptr<const zx_iovec_t> user_data,
                           size_t* out_actual);
    zx_status_t WriteVector(size_t vector_count, user_in_ptr<const zx_iovec_t> user_data,
                            size_t* out_actual);
    zx_status_t Seek(zx_stream_seek_origin_t whence, int64_t offset, zx_off_t* out_seek);
    void GetInfo(zx_info_stream_t* info) const;

private:
    explicit StreamDispatcher(uint32_t options, fbl::RefPtr<VmObjectDispatcher> vmo,
                              zx_off_t seek);

    zx_status_t UserIovecTotal(user_in_ptr<const zx_iovec_t> user_data, size_t vector_count,
                               size_t* total_capacity) const;
    zx_status_t ReadVectorAt(user_in_ptr<const zx_iovec_t> user_data, size_t vector_count,
                             zx_off_t offset, size_t length);
    zx_status_t WriteVectorAt(user_in_ptr<const zx_iovec_t> user_data, size_t vector_count,
                              zx_off_t offset, size_t length);

    fbl::Canary<fbl::magic("STRM")> canary_;

    const uint32_t options_;
    const fbl::RefPtr<VmObjectDispatcher> vmo_;
    zx_off_t seek_ TA_GUARDED(get_lock()) = 0u;
};
