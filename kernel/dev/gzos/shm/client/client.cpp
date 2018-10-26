// Copyright 2018 Open Trust Group
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <string.h>

#include <arch/arm64/smccc.h>
#include <dev/gzos_shm.h>
#include <lk/init.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <zircon/syscalls/smc_defs.h>

static ns_shm_info_t ns_shm;

extern "C" void gzos_shm_get_config(ns_shm_info_t* shm) {
    if (shm) {
        memcpy(shm, &ns_shm, sizeof(ns_shm_info_t));
    }
}

static uint64_t trusty_get_shm_info(uint32_t param) {
    return arm_smccc_smc(SMC_FC_GET_STATIC_SHM_CONFIG, param, 0, 0, 0, 0, 0, 0).x0;
}

static void MarkPagesInUsePhys(zx_paddr_t paddr, size_t size) {
    static list_node reserved_page_list = LIST_INITIAL_VALUE(reserved_page_list);
    size_t pages = ROUNDUP_PAGE_SIZE(size) / PAGE_SIZE;
    zx_status_t status = pmm_alloc_range(paddr, pages, &reserved_page_list);
    ASSERT(status == ZX_OK);

    // mark all of the pages we allocated as WIRED
    vm_page_t* p;
    list_for_every_entry (&reserved_page_list, p, vm_page_t, queue_node) {
        p->state = VM_PAGE_STATE_WIRED;
    }
}

static void ns_shm_init(uint level) {
    ns_shm.pa = trusty_get_shm_info(TRUSTY_SHM_PA);
    ns_shm.size = trusty_get_shm_info(TRUSTY_SHM_SIZE);
    ns_shm.use_cache = trusty_get_shm_info(TRUSTY_SHM_USE_CACHE);
    ASSERT(ns_shm.size > 0);

    MarkPagesInUsePhys(ns_shm.pa, ns_shm.size);
}
LK_INIT_HOOK(ns_shm_init, ns_shm_init, LK_INIT_LEVEL_PLATFORM_EARLY);
