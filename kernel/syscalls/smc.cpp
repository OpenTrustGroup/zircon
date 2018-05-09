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
#include <object/vm_object_dispatcher.h>

#include <object/c_user_smc_service.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/policy.h>

#include "priv.h"

using fbl::AutoLock;

#define LOCAL_TRACE 1

zx_status_t sys_smc_create(uint32_t options,
                           user_out_ptr<void> user_buffer,
                           uint32_t len,
                           user_out_handle* smc_out,
                           user_out_handle* vmo_out) {
    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryPolicy(ZX_POL_NEW_SMC);
    if (res != ZX_OK) return res;

    fbl::RefPtr<SmcDispatcher> smc_disp;
    fbl::RefPtr<VmObject> shm_vmo;
    zx_rights_t smc_rights;
    res = SmcDispatcher::Create(options, &smc_disp, &smc_rights, &shm_vmo);
    if (res != ZX_OK) return res;

    fbl::RefPtr<Dispatcher> vmo_disp;
    zx_rights_t vmo_rights;
    res = VmObjectDispatcher::Create(fbl::move(shm_vmo), &vmo_disp, &vmo_rights);
    if (res != ZX_OK) return res;

    zx_info_smc_t smc_info = smc_disp->GetSmcInfo();
    if (len != sizeof(zx_info_smc_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    res = user_buffer.copy_array_to_user(static_cast<void*>(&smc_info), sizeof(zx_info_smc_t));
    if (res != ZX_OK) return ZX_ERR_INVALID_ARGS;

    res = smc_out->make(fbl::move(smc_disp), smc_rights);
    if (res == ZX_OK) {
        vmo_rights = ZX_RIGHTS_IO | ZX_RIGHT_MAP;
        res = vmo_out->make(fbl::move(vmo_disp), vmo_rights);
    }

    return res;
}

zx_status_t sys_smc_wait_for_request(zx_handle_t smc_handle,
                                     user_out_ptr<void> user_buffer,
                                     uint32_t len) {
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

zx_status_t sys_smc_call_test(zx_handle_t smc_handle,
                              user_inout_ptr<void> user_buffer,
                              uint32_t len,
                              user_out_ptr<long> smc_ret) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SmcDispatcher> smc;
    zx_status_t status = up->GetDispatcherWithRights(smc_handle, ZX_RIGHTS_IO, &smc);
    if (status != ZX_OK)
        return status;

    if (len != sizeof(smc32_args_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (smc_ret.get() == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    smc32_args_t args = {};
    status = user_buffer.copy_array_from_user(static_cast<void*>(&args),
                                              sizeof(smc32_args_t));
    if (status != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    long ret = notify_smc_service(&args);

    status = user_buffer.copy_array_to_user(static_cast<void*>(&args),
                                            sizeof(smc32_args_t));
    if (status != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    return smc_ret.copy_to_user(ret);
}
