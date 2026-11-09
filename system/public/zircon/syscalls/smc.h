// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// The owning entity (service call range) of an SMC function ID is bits 29:24
// of the function ID.
#define ARM_SMC_SERVICE_CALL_NUM_SHIFT 24
#define ARM_SMC_SERVICE_CALL_NUM_MASK 0x3F

// The arguments of a secure monitor call, as later Zircon's zx_smc_call
// takes them (SMC Calling Convention: the function ID, six arguments, and
// the client and secure OS IDs).
typedef struct zx_smc_parameters {
    uint32_t func_id;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
    uint16_t client_id;
    uint16_t secure_os_id;
} zx_smc_parameters_t;

// What a secure monitor call returns: x0 to x3, and x6.
typedef struct zx_smc_result {
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg6;
} zx_smc_result_t;

__END_CDECLS
