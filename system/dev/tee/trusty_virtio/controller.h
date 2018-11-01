// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>

#include <zircon/syscalls/smc.h>
#include <zircon/syscalls/smc_defs.h>

#include "shared_memory.h"
#include "smc.h"
#include "trace.h"

#pragma once

namespace trusty_virtio {

class Controller {
public:
    static Controller* Instance() {
        static fbl::Mutex alloc_lock;
        fbl::AutoLock lock(&alloc_lock);
        if (!instance_) {
            instance_ = fbl::make_unique<Controller>();
            ZX_ASSERT(instance_ != nullptr);
        }
        return instance_.get();
    }

    Controller()
        : secure_monitor_(get_root_resource()) {}

    SharedMemoryPool& shm_pool() { return shm_pool_; }

    zx_status_t monitor_std_call(uint32_t cmd, zx_smc_result_t* result, uint32_t args0 = 0,
                                 uint32_t args1 = 0, uint32_t args2 = 0);

    zx_status_t monitor_nop_call(uint32_t cmd, uint32_t args0 = 0,
                                 uint32_t args1 = 0);

private:
    SharedMemoryPool shm_pool_;
    zx_handle_t secure_monitor_;

    static fbl::unique_ptr<Controller> instance_;
};

} // namespace trusty_virtio
