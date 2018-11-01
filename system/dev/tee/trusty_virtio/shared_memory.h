// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>

#include <limits.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <region-alloc/region-alloc.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

namespace trusty_virtio {

class SharedMemory : public fbl::DoublyLinkedListable<fbl::unique_ptr<SharedMemory>> {
public:
    using RegionPtr = RegionAllocator::Region::UPtr;

    explicit SharedMemory(zx_vaddr_t base_vaddr, zx_paddr_t base_paddr, RegionPtr region);

    // Move only type
    SharedMemory(SharedMemory&&) = default;
    SharedMemory& operator=(SharedMemory&&) = default;

    zx_vaddr_t vaddr() const { return base_vaddr_ + region_->base; }
    zx_paddr_t paddr() const { return base_paddr_ + region_->base; }
    size_t size() const { return region_->size; }

    template <typename T>
    T* as(uintptr_t off) const {
        ZX_ASSERT(off + sizeof(T) <= region_->size);
        return reinterpret_cast<T*>(vaddr() + off);
    }

private:
    zx_vaddr_t base_vaddr_;
    zx_paddr_t base_paddr_;

    RegionPtr region_;
};

class SharedMemoryPool {
public:
    SharedMemoryPool();

    zx_status_t Allocate(size_t size, fbl::unique_ptr<SharedMemory>* out_shared_memory) {
        // The RegionAllocator provides thread safety around allocations, so we currently don't
        // require any additional locking.

        // Let's try to carve off a region first.
        auto region = region_allocator_.GetRegion(size, kAlignment);
        if (!region) {
            return ZX_ERR_NO_RESOURCES;
        }

        fbl::AllocChecker ac;
        auto shared_memory = fbl::make_unique_checked<SharedMemory>(
            &ac, vaddr_, paddr_, fbl::move(region));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        *out_shared_memory = fbl::move(shared_memory);
        return ZX_OK;
    }

private:
    static constexpr uint64_t kAlignment = PAGE_SIZE;

    zx_vaddr_t vaddr_;
    zx_paddr_t paddr_;
    RegionAllocator region_allocator_;

    zx::resource shm_rsc_;
    zx::vmo vmo_;
};

} // namespace trusty_virtio
