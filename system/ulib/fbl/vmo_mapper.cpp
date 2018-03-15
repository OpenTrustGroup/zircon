// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/vmo_mapper.h>
#include <zircon/assert.h>

namespace fbl {

zx_status_t VmoMapper::CreateAndMap(uint64_t size,
                                    uint32_t map_flags,
                                    RefPtr<VmarManager> vmar_manager,
                                    zx::vmo* vmo_out,
                                    zx_rights_t vmo_rights,
                                    uint32_t cache_policy) {
    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t res = CheckReadyToMap(vmar_manager);
    if (res != ZX_OK) {
        return res;
    }

    zx::vmo vmo;
    zx_status_t ret = zx::vmo::create(size, 0, &vmo);
    if (ret != ZX_OK) {
        return ret;
    }

    if (cache_policy != 0) {
        ret = vmo.set_cache_policy(cache_policy);
        if (ret != ZX_OK) {
            return ret;
        }
    }

    ret = InternalMap(vmo, size, map_flags, fbl::move(vmar_manager));
    if (ret != ZX_OK) {
        return ret;
    }

    if (vmo_out) {
        if (vmo_rights != ZX_RIGHT_SAME_RIGHTS) {
            ret = vmo.replace(vmo_rights, &vmo);
            if (ret != ZX_OK) {
                Unmap();
                return ret;
            }
        }

        *vmo_out = fbl::move(vmo);
    }

    return ZX_OK;
}

zx_status_t VmoMapper::Map(const zx::vmo& vmo,
                           uint64_t size,
                           uint32_t map_flags,
                           RefPtr<VmarManager> vmar_manager) {
    zx_status_t res;

    if (!vmo.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    res = CheckReadyToMap(vmar_manager);
    if (res != ZX_OK) {
        return res;
    }

    if (!size) {
        res = vmo.get_size(&size);
        if (res != ZX_OK) {
            return res;
        }
    }
    return InternalMap(vmo, size, map_flags, vmar_manager);
}

void VmoMapper::Unmap() {
    if (start_ != nullptr) {
        ZX_DEBUG_ASSERT(size_ != 0);
        zx_handle_t vmar_handle = (vmar_manager_ == nullptr)
            ? zx::vmar::root_self().get()
            : vmar_manager_->vmar().get();

        __UNUSED zx_status_t res;
        res = zx_vmar_unmap(vmar_handle, reinterpret_cast<uintptr_t>(start_), size_);
        ZX_DEBUG_ASSERT(res == ZX_OK);
    }

    vmar_manager_.reset();
    start_ = nullptr;
    size_ = 0;
}

zx_status_t VmoMapper::CheckReadyToMap(const RefPtr<VmarManager>& vmar_manager) {
    if (start_ != nullptr) {
        return ZX_ERR_BAD_STATE;
    }

    if ((vmar_manager != nullptr) && !vmar_manager->vmar().is_valid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
}

zx_status_t VmoMapper::InternalMap(const zx::vmo& vmo,
                                   uint64_t size,
                                   uint32_t map_flags,
                                   RefPtr<VmarManager> vmar_manager) {
    ZX_DEBUG_ASSERT(vmo.is_valid());
    ZX_DEBUG_ASSERT(start_ == nullptr);
    ZX_DEBUG_ASSERT(size_ == 0);
    ZX_DEBUG_ASSERT(vmar_manager_ == nullptr);

    uintptr_t tmp;
    zx_handle_t vmar_handle = (vmar_manager == nullptr)
        ? zx::vmar::root_self().get()
        : vmar_manager->vmar().get();

    zx_status_t res = zx_vmar_map(vmar_handle, 0, vmo.get(), 0, size, map_flags, &tmp);
    if (res != ZX_OK) {
        return res;
    }

    start_ = reinterpret_cast<void*>(tmp);
    size_ = size;
    vmar_manager_ = fbl::move(vmar_manager);

    return ZX_OK;
}

} // namespace fbl
