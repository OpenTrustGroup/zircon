// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

// The number of the CPU whose hardware ID is |hwid|.
zx_status_t _zx_system_get_cpu_id(uint8_t* cpu_id, uint64_t hwid) {
    for (uint32_t cpu = 0; cpu < DATA_CONSTANTS.max_num_cpus && cpu < VDSO_MAX_NUM_CPUS; cpu++) {
        if (DATA_CONSTANTS.cpu_hwid[cpu] == hwid) {
            *cpu_id = static_cast<uint8_t>(cpu);
            return ZX_OK;
        }
    }
    return ZX_ERR_NOT_FOUND;
}

VDSO_INTERFACE_FUNCTION(zx_system_get_cpu_id);
