// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <fbl/algorithm.h>
#include <virtio/trusty.h>
#include <zircon/syscalls/smc.h>

#pragma once

namespace trusty_virtio {

class NonSecurePageInfo {
public:
    NonSecurePageInfo(zx_paddr_t paddr) {
        page_info_ = fbl::round_down(paddr, static_cast<uint32_t>(PAGE_SIZE));
        page_info_ |= MemAttr(NS_MAIR_NORMAL_CACHED_WB_RWA, NS_INNER_SHAREABLE);
    }

    uint32_t low() { return static_cast<uint32_t>(page_info_); }
    uint32_t high() { return static_cast<uint32_t>(page_info_ >> 32); }

private:
    static uint64_t MemAttr(uint64_t mair, uint64_t shareable) {
        return (mair << 48) | (shareable << 8);
    }

    uint64_t page_info_;
};

// C++ wrapper function for constructing a zx_smc_parameters_t object. Most of the arguments are
// rarely used, so this defaults everything other than the function id to 0. Most of the function
// calls are also constant, so they should be populated at compile time if possible.
static constexpr zx_smc_parameters_t CreateSmcFunctionCall(
    uint32_t func_id,
    uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0,
    uint64_t arg4 = 0, uint64_t arg5 = 0, uint64_t arg6 = 0,
    uint16_t client_id = 0, uint16_t secure_os_id = 0) {
    return {func_id, arg1, arg2, arg3, arg4, arg5, arg6, client_id, secure_os_id};
}

} // namespace trusty_virtio
