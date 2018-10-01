// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/smc.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t smc::create(uint32_t options,
                        smc* result) {
    smc smc;
    zx_status_t status = zx_smc_create(options, smc.reset_and_get_address());
    result->reset(smc.release());
    return status;
}

} // namespace zx
