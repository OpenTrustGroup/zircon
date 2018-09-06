// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared-memory.h"

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>

namespace optee {

SharedMemory::SharedMemory(zx_vaddr_t base_vaddr, zx_paddr_t base_paddr, RegionPtr region)
    : base_vaddr_(base_vaddr), base_paddr_(base_paddr), region_(fbl::move(region)) {}

zx_status_t SharedMemoryManager::Create(zx_paddr_t shared_mem_start,
                                        size_t shared_mem_size,
                                        fbl::unique_ptr<io_buffer_t> secure_world_memory,
                                        fbl::unique_ptr<SharedMemoryManager>* out_manager) {
    ZX_DEBUG_ASSERT(secure_world_memory != nullptr);
    ZX_DEBUG_ASSERT(out_manager != nullptr);

    auto io_buffer_cleanup = fbl::MakeAutoCall([io_buffer = secure_world_memory.get()]() {
        io_buffer_release(io_buffer);
    });

    // Round the start and end to the nearest page boundaries within the range and calculate a
    // new size.
    shared_mem_start = fbl::round_up(shared_mem_start, static_cast<uint32_t>(PAGE_SIZE));
    const zx_paddr_t shared_mem_end = fbl::round_down(shared_mem_start + shared_mem_size,
                                                      static_cast<uint32_t>(PAGE_SIZE));
    if (shared_mem_end <= shared_mem_start) {
        zxlogf(ERROR, "optee: no shared memory available from secure world\n");
        return ZX_ERR_NO_RESOURCES;
    }
    shared_mem_size = shared_mem_end - shared_mem_start;

    // The secure world shared memory exists within some subrange of the secure_world_memory.
    // Get the addresses from the io_buffer and validate that the requested subrange is within
    // the mmio range.
    const zx_vaddr_t secure_world_vaddr = reinterpret_cast<zx_vaddr_t>(io_buffer_virt(
        secure_world_memory.get()));
    const zx_paddr_t secure_world_paddr = io_buffer_phys(secure_world_memory.get());
    const size_t secure_world_size = io_buffer_size(secure_world_memory.get(), 0);

    if ((shared_mem_start < secure_world_paddr) ||
        (shared_mem_end > secure_world_paddr + secure_world_size)) {
        zxlogf(ERROR, "optee: shared memory not within secure os memory\n");
        return ZX_ERR_INTERNAL;
    }

    if (shared_mem_size < 2 * kDriverPoolSize) {
        zxlogf(ERROR, "optee: shared memory is not large enough\n");
        return ZX_ERR_NO_RESOURCES;
    }

    const zx_off_t shared_mem_offset = shared_mem_start - secure_world_paddr;

    fbl::AllocChecker ac;
    fbl::unique_ptr<SharedMemoryManager> manager(new (&ac) SharedMemoryManager(
        secure_world_vaddr + shared_mem_offset,
        secure_world_paddr + shared_mem_offset,
        shared_mem_size,
        fbl::move(secure_world_memory)));

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // We've successfully created the Manager and it now owns the io_buffer memory
    io_buffer_cleanup.cancel();

    *out_manager = fbl::move(manager);
    return ZX_OK;
}

SharedMemoryManager::~SharedMemoryManager() {
    io_buffer_release(secure_world_memory_.get());
}

SharedMemoryManager::SharedMemoryManager(zx_vaddr_t base_vaddr,
                                         zx_paddr_t base_paddr,
                                         size_t total_size,
                                         fbl::unique_ptr<io_buffer_t> secure_world_memory)
    : secure_world_memory_(fbl::move(secure_world_memory)),
      driver_pool_(base_vaddr, base_paddr, kDriverPoolSize),
      client_pool_(base_vaddr + kDriverPoolSize,
                   base_paddr + kDriverPoolSize,
                   total_size - kDriverPoolSize) {}

} // namespace optee
