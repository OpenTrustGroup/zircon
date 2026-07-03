// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_PAGER_PAGER_WRAPPER_H_
#define ZIRCON_SYSTEM_UTEST_CORE_PAGER_PAGER_WRAPPER_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>

namespace zx {

class pager : public object<pager> {
public:
    static constexpr zx_obj_type_t TYPE = (zx_obj_type_t)32; // ZX_OBJ_TYPE_PAGER

    constexpr pager() = default;
    explicit pager(zx_handle_t value) : object(value) {}
    explicit pager(handle&& h) : object(h.release()) {}

    static zx_status_t create(uint32_t options, pager* result) {
        return zx_pager_create(options, result->reset_and_get_address());
    }

    zx_status_t create_vmo(uint32_t options, const object_base& port, uint64_t key, uint64_t size,
                           vmo* result) const {
        return zx_pager_create_vmo(get(), options, port.get(), key, size,
                                   result->reset_and_get_address());
    }

    zx_status_t detach_vmo(const vmo& vmo_handle) const {
        return zx_pager_detach_vmo(get(), vmo_handle.get());
    }

    zx_status_t supply_pages(const vmo& vmo_handle, uint64_t offset, uint64_t length,
                             const vmo& aux_vmo, uint64_t aux_offset) const {
        return zx_pager_supply_pages(get(), vmo_handle.get(), offset, length, aux_vmo.get(),
                                     aux_offset);
    }

    zx_status_t op_range(uint32_t op, const vmo& vmo_handle, uint64_t offset, uint64_t length,
                         uint64_t data) const {
        return zx_pager_op_range(get(), op, vmo_handle.get(), offset, length, data);
    }

    zx_status_t query_dirty_ranges(const vmo& vmo_handle, uint64_t offset, uint64_t length,
                                   void* buffer, size_t buffer_size, uint64_t* actual_count,
                                   uint64_t* remaining_count) const {
        return zx_pager_query_dirty_ranges(get(), vmo_handle.get(), offset, length, buffer,
                                           buffer_size, actual_count, remaining_count);
    }
};

} // namespace zx

#endif  // ZIRCON_SYSTEM_UTEST_CORE_PAGER_PAGER_WRAPPER_H_
