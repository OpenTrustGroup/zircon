/*
 * Copyright (c) 2013-2016 Google Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#include <zircon/syscalls/smc_defs.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct smc32_args {
    uint32_t smc_nr;
    uint32_t params[SMC_NUM_PARAMS];
} smc32_args_t;

#define SMC32_ARGS_INITIAL_VALUE(args) {0, {0}}

typedef struct zx_info_ns_shm {
    uint32_t base_phys;
    uint32_t size;
    bool use_cache;
} zx_info_ns_shm_t;

typedef struct zx_info_smc {
    zx_info_ns_shm_t ns_shm;
} zx_info_smc_t;

__END_CDECLS
