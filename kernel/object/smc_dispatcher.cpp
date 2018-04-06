// Copyright 2018 Open Trust Group
// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/smc_dispatcher.h>
#include <object/c_user_smc_service.h>

#include <zircon/rights.h>

#include <err.h>
#include <string.h>
#include <trace.h>

#include <kernel/thread.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#define LOCAL_TRACE 0
#define ENABLE_SMC_TEST 1

using fbl::AutoLock;

static fbl::Mutex alloc_lock;
static fbl::RefPtr<SmcDispatcher> smc_disp TA_GUARDED(alloc_lock);

#if ENABLE_SMC_TEST
static int fake_smc(void* arg) {
    /* clear fake smc signal to prevent from triggering fake smc again */
    smc_disp->user_signal(ZX_SMC_FAKE_REQUEST, 0, 0);

    /* clear test result signal */
    smc_disp->user_signal(ZX_SMC_TEST_PASS | ZX_SMC_TEST_FAIL, 0, 0);

    smc32_args_t smc_args = {
        .smc_nr = 0x534d43UL,
        .params = {0x70617230UL, 0x70617231UL, 0x70617232UL}
    };

    long result = notify_smc_service(&smc_args);
    if (result == (long)smc_args.smc_nr) {
        smc_disp->user_signal(0, ZX_SMC_TEST_PASS, 0);
    } else {
        smc_disp->user_signal(0, ZX_SMC_TEST_FAIL, 0);
    }

    return 0;
}

static void generate_fake_smc_request() {
    thread_t* fake_smc_thread = thread_create("fake_smc", fake_smc, NULL,
            DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
    if (!fake_smc_thread) {
        panic("failed to create fake smc thread\n");
    }

    thread_detach_and_resume(fake_smc_thread);
}

class FakeSmcRequest final : public SmcObserver {
public:
    Flags OnStateChange(zx_signals_t new_state) override {
        LTRACEF("new_state (0x%08x)\n", new_state);
        if (new_state & ZX_SMC_FAKE_REQUEST) {
            generate_fake_smc_request();
        }
        return 0;
    }
};
#endif

zx_status_t SmcDispatcher::Create(uint32_t options, fbl::RefPtr<SmcDispatcher>* dispatcher,
                                  zx_rights_t* rights) {
    AutoLock lock(&alloc_lock);

    if (smc_disp == nullptr) {
        fbl::AllocChecker ac;
        auto disp = new (&ac) SmcDispatcher(options);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

#if ENABLE_SMC_TEST
        auto obs = new (&ac) FakeSmcRequest();
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        disp->add_observer(obs);
#endif

        smc_disp = fbl::AdoptRef<SmcDispatcher>(disp);
    }

    *rights = ZX_DEFAULT_SMC_RIGHTS;
    *dispatcher = smc_disp;

    return ZX_OK;
}

SmcDispatcher::SmcDispatcher(uint32_t options)
    : options(options), smc_args(nullptr), smc_result(SM_ERR_INTERNAL_FAILURE) {
    event_init(&request_event_, false, EVENT_FLAG_AUTOUNSIGNAL);
    event_init(&result_event_, false, EVENT_FLAG_AUTOUNSIGNAL);
}

zx_status_t SmcDispatcher::NotifyUser(smc32_args_t* args) {
    canary_.Assert();

    AutoLock lock(get_lock());

    zx_signals_t signals = GetSignalsStateLocked();
    if ((signals & ZX_SMC_REQUEST) == 0) {
        smc_args = args;
        UpdateStateLocked(0, ZX_SMC_REQUEST);
        event_signal(&request_event_, false);
        return ZX_OK;
    }

    return ZX_ERR_BAD_STATE;
}

long SmcDispatcher::WaitForResult() {
    canary_.Assert();

    zx_status_t status = event_wait_deadline(&result_event_, ZX_TIME_INFINITE, true);

    AutoLock lock(get_lock());

    long result = SM_ERR_INTERNAL_FAILURE;
    zx_signals_t signals = GetSignalsStateLocked();

    if (signals & ZX_SMC_REQUEST) {
        if (status == ZX_OK) {
            result = smc_result;
        }
        UpdateStateLocked(ZX_SMC_REQUEST, 0);
    }

    return result;
}

zx_status_t SmcDispatcher::WaitForRequest(smc32_args_t* args) {
    canary_.Assert();

    if (!args) return ZX_ERR_INVALID_ARGS;

    zx_status_t status = event_wait_deadline(&request_event_, ZX_TIME_INFINITE, true);
    if (status != ZX_OK) return status;

    AutoLock lock(get_lock());

    zx_signals_t signals = GetSignalsStateLocked();
    if ((signals & ZX_SMC_SIGNALED) == 0) {
        memcpy(args, smc_args, sizeof(smc32_args_t));
        UpdateStateLocked(0, ZX_SMC_SIGNALED);
        return ZX_OK;
    }

    return ZX_ERR_BAD_STATE;
}

zx_status_t SmcDispatcher::SetResult(long result) {
    canary_.Assert();

    AutoLock lock(get_lock());

    zx_signals_t signals = GetSignalsStateLocked();
    if (signals & ZX_SMC_SIGNALED) {
        smc_result = result;
        UpdateStateLocked(ZX_SMC_SIGNALED, 0);
        event_signal(&result_event_, false);
        return ZX_OK;
    }

    return ZX_ERR_BAD_STATE;
}

long notify_smc_service(smc32_args_t* args) {
    if (args == nullptr) return SM_ERR_INVALID_PARAMETERS;

    if (smc_disp == nullptr) return smc_undefined(args);

    zx_status_t status = smc_disp->NotifyUser(args);
    if (status != ZX_OK) return SM_ERR_BUSY;

    return smc_disp->WaitForResult();
}

