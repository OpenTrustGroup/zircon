// Copyright 2018 Open Trust Group
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
#include <object/vm_object_dispatcher.h>

#include <object/c_user_smc_service.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/policy.h>

#include "priv.h"


#define LOCAL_TRACE 1

zx_status_t sys_smc_create(uint32_t options,
                           user_out_handle* smc_out) {
    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryPolicy(ZX_POL_NEW_SMC);
    if (res != ZX_OK) return res;

    fbl::RefPtr<SmcDispatcher> smc_disp;
    zx_rights_t smc_rights;
    res = SmcDispatcher::Create(options, &smc_disp, &smc_rights);
    if (res != ZX_OK) return res;

    return smc_out->make(fbl::move(smc_disp), smc_rights);
}

zx_status_t sys_smc_read(zx_handle_t smc_handle,
                         user_out_ptr<smc32_args_t> user_args) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SmcDispatcher> smc;
    zx_status_t status = up->GetDispatcherWithRights(smc_handle, ZX_RIGHT_READ, &smc);
    if (status != ZX_OK) return status;

    smc32_args_t args = {};

    status = smc->ReadArgs(&args);
    if (status != ZX_OK) return status;

    status = user_args.copy_to_user(args);
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

zx_status_t sys_smc_call_test(zx_handle_t smc_handle,
                              user_inout_ptr<smc32_args_t> user_args,
                              user_out_ptr<long> smc_ret) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SmcDispatcher> smc;
    zx_status_t status = up->GetDispatcherWithRights(smc_handle, ZX_RIGHTS_IO, &smc);
    if (status != ZX_OK)
        return status;

    if (smc_ret.get() == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    smc32_args_t args = {};
    status = user_args.copy_from_user(&args);
    if (status != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    long ret = notify_smc_service(&args);

    status = user_args.copy_to_user(args);
    if (status != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    return smc_ret.copy_to_user(ret);
}

zx_status_t sys_smc_nop_call_test(zx_handle_t smc_handle,
                                  user_in_ptr<const smc32_args_t> user_args) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SmcDispatcher> smc;
    zx_status_t status = up->GetDispatcherWithRights(smc_handle, ZX_RIGHTS_IO, &smc);
    if (status != ZX_OK)
        return status;

    smc32_args_t args = {};
    status = user_args.copy_from_user(&args);
    if (status != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    long ret = notify_nop_thread(&args);
    if (ret != SM_OK) {
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t sys_smc_read_nop(zx_handle_t smc_handle, uint32_t cpu_num,
                             user_out_ptr<smc32_args_t> user_args) {
    if (!is_valid_cpu_num(cpu_num))
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SmcDispatcher> smc;
    zx_status_t status = up->GetDispatcherWithRights(smc_handle, ZX_RIGHT_READ, &smc);
    if (status != ZX_OK) return status;

    smc32_args_t args = {};

    // Pin to the target CPU
    thread_set_cpu_affinity(get_current_thread(), cpu_num_to_mask(cpu_num));

    status = smc->ReadNopRequest(cpu_num, &args);
    if (status != ZX_OK) return status;

    status = user_args.copy_to_user(args);
    if (status != ZX_OK) return ZX_ERR_INVALID_ARGS;

    return ZX_OK;
}

zx_status_t sys_smc_cancel_read_nop(zx_handle_t smc_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SmcDispatcher> smc;
    zx_status_t status = up->GetDispatcherWithRights(smc_handle, ZX_RIGHTS_IO, &smc);
    if (status != ZX_OK) return status;

    return smc->CancelReadNopRequest();
}
