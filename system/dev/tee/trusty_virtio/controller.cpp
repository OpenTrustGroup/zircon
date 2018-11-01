// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/ref_counted.h>

#include <zircon/syscalls/smc_defs.h>

#include "controller.h"
#include "shared_memory.h"
#include "smc.h"
#include "trace.h"

namespace trusty_virtio {

fbl::unique_ptr<Controller> Controller::instance_;

zx_status_t Controller::monitor_std_call(uint32_t cmd, zx_smc_result_t* out,
                                         uint32_t args0, uint32_t args1, uint32_t args2) {
    zx_smc_parameters_t params = CreateSmcFunctionCall(cmd, args0, args1, args2);
    zx_smc_result_t res;
    zx_status_t status = zx_smc_call(secure_monitor_, &params, &res);

    while (1) {
        if (status != ZX_OK) {
            return status;
        }

        int sm_err = static_cast<int>(res.arg0);
        if (sm_err < 0) {
            if (sm_err != SM_ERR_INTERRUPTED) {
                TRACEF("SM returns error (%d)\n", sm_err);
                return ZX_ERR_INTERNAL;
            }
        } else {
            break;
        }

        zx_smc_parameters_t resume = CreateSmcFunctionCall(SMC_SC_RESTART_LAST);
        status = zx_smc_call(secure_monitor_, &resume, &res);
    }

    if (out) {
        *out = res;
    }
    return ZX_OK;
}

zx_status_t Controller::monitor_nop_call(uint32_t cmd, uint32_t args0,
                                         uint32_t args1) {
    zx_smc_parameters_t params = CreateSmcFunctionCall(SMC_SC_NOP, cmd, args0, args1);
    zx_smc_result_t res;
    zx_status_t status = zx_smc_call(secure_monitor_, &params, &res);

    while (1) {
        if (status != ZX_OK) {
            return status;
        }

        int sm_err = static_cast<int>(res.arg0);
        if (sm_err == SM_ERR_NOP_DONE) {
            break;
        }
        if (sm_err < 0) {
            if (sm_err != SM_ERR_NOP_INTERRUPTED) {
                TRACEF("SM(nop) returns error (%d)\n", sm_err);
                return ZX_ERR_INTERNAL;
            }
        }

        zx_smc_parameters_t resume = CreateSmcFunctionCall(SMC_SC_NOP);
        status = zx_smc_call(secure_monitor_, &resume, &res);
    }

    return ZX_OK;
}

} // namespace trusty_virtio
