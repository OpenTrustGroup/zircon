// Copyright 2018 Open Trust Group
// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <lib/user_copy/user_ptr.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/smc_dispatcher.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <zircon/syscalls/policy.h>

#include "priv.h"

using fbl::AutoLock;

#define LOCAL_TRACE 1

zx_status_t sys_smc_create(uint32_t options, user_out_handle* out) {
    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryPolicy(ZX_POL_NEW_SMC);
    if (res != ZX_OK) return res;

    fbl::RefPtr<SmcDispatcher> smc;
    zx_rights_t rights;
    zx_status_t result = SmcDispatcher::Create(options, &smc, &rights);

    if (result == ZX_OK) {
        result = out->make(fbl::move(smc), rights);
    }
    return result;
}

zx_status_t sys_smc_wait_for_request(zx_handle_t smc_handle,
                                user_out_ptr<void> user_buffer, uint32_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SmcDispatcher> smc;
    zx_status_t status = up->GetDispatcherWithRights(smc_handle, ZX_RIGHT_READ, &smc);
    if (status != ZX_OK) return status;

    smc32_args_t args = {};
    if (len != sizeof(smc32_args_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    status = smc->WaitForRequest(&args);
    if (status != ZX_OK) return status;

    status = user_buffer.copy_array_to_user(static_cast<void*>(&args), sizeof(smc32_args_t));
    if (status != ZX_OK) return ZX_ERR_INVALID_ARGS;

    return ZX_OK;
}

zx_status_t sys_smc_set_result(zx_handle_t smc_handle, long smc_result) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SmcDispatcher> smc;
    zx_status_t status = up->GetDispatcherWithRights(smc_handle, ZX_RIGHT_WRITE, &smc);
    if (status != ZX_OK)
        return status;

    return smc->SetResult(smc_result);
}
