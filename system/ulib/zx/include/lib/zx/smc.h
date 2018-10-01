// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

class smc : public object<smc> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_SMC;

    constexpr smc() = default;

    explicit smc(zx_handle_t value) : object(value) {}

    explicit smc(handle&& h) : object(h.release()) {}

    smc(smc&& other) : object(other.release()) {}

    smc& operator=(smc&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(uint32_t flags, smc* smc);

    zx_status_t read(smc32_args_t* args) const {
        return zx_smc_read(get(), args);
    }

    zx_status_t set_result(long result) const {
        return zx_smc_set_result(get(), result);
    }

    zx_status_t read_nop(uint32_t cpu_num, smc32_args_t* args) const {
        return zx_smc_read_nop(get(), cpu_num, args);
    }

    zx_status_t cancel_read_nop() const {
        return zx_smc_cancel_read_nop(get());
    }
};

using unowned_smc = unowned<smc>;

} // namespace zx
