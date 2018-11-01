// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared_memory.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <fbl/limits.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls/resource.h>

namespace trusty_virtio {

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE;

SharedMemory::SharedMemory(zx_vaddr_t base_vaddr, zx_paddr_t base_paddr, RegionPtr region)
    : base_vaddr_(base_vaddr), base_paddr_(base_paddr), region_(fbl::move(region)) {}

SharedMemoryPool::SharedMemoryPool()
    : region_allocator_(
          RegionAllocator::RegionPool::Create(fbl::numeric_limits<size_t>::max())) {
    const char rsc_name[] = "ns_shm";

    zx_handle_t rsc;
    zx_status_t status = zx_resource_create(get_root_resource(),
                                            ZX_RSRC_KIND_NSMEM,
                                            0, 0, rsc_name, sizeof(rsc_name), &rsc);
    ZX_ASSERT(status == ZX_OK);
    shm_rsc_.reset(rsc);

    zx_info_resource_t info;
    status = shm_rsc_.get_info(ZX_INFO_RESOURCE, &info, sizeof(info), nullptr, nullptr);
    ZX_ASSERT(status == ZX_OK);

    status = zx::vmo::create_ns_mem(shm_rsc_, info.base, info.size, &vmo_);
    ZX_ASSERT(status == ZX_OK);

    region_allocator_.AddRegion({.base = 0, .size = info.size});

    status = zx::vmar::root_self()->map(0, vmo_, 0, info.size, kMapFlags, &vaddr_);
    ZX_ASSERT(status == ZX_OK);

    paddr_ = info.base;
}

} // namespace trusty_virtio
