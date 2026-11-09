// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

// The hardware ID (MPIDR affinity fields on arm64) of CPU |cpu|.
zx_status_t _zx_system_get_cpu_hwid(uint64_t* hwid, uint32_t cpu) {
    if (cpu >= DATA_CONSTANTS.max_num_cpus || cpu >= VDSO_MAX_NUM_CPUS)
        return ZX_ERR_OUT_OF_RANGE;
    *hwid = DATA_CONSTANTS.cpu_hwid[cpu];
    return ZX_OK;
}

VDSO_INTERFACE_FUNCTION(zx_system_get_cpu_hwid);
