// Copyright 2018 Open Trust Group
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>

#include <dev/gzos_shm.h>
#include <pdev/driver.h>
#include <zircon/boot/driver-config.h>

static ns_shm_info_t ns_shm;

void gzos_shm_get_config(ns_shm_info_t* shm) {
    if (shm) {
        memcpy(shm, &ns_shm, sizeof(ns_shm_info_t));
    }
}

static void ns_shm_init(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_sm_ns_shm_t));
    const dcfg_sm_ns_shm_t* ns_shm_cfg = driver_data;

    ASSERT(ns_shm_cfg->length > 0);
    ns_shm.pa = ns_shm_cfg->base_phys;
    ns_shm.size = ns_shm_cfg->length;
    ns_shm.use_cache = ns_shm_cfg->use_cache;
}

LK_PDEV_INIT(ns_shm_init, KDRV_SM_NS_SHM, ns_shm_init, LK_INIT_LEVEL_PLATFORM_EARLY);
