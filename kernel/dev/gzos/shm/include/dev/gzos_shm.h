// Copyright 2018 Open Trust Group
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

typedef struct ns_shm_info {
    uint32_t pa;
    uint32_t size;
    bool use_cache;
} ns_shm_info_t;

__BEGIN_CDECLS

/* Get Non-secure share memory configuration */
void gzos_shm_get_config(ns_shm_info_t* shm);

__END_CDECLS
