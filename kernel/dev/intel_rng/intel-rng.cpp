// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/hw_rng.h>

#include <arch/x86/feature.h>
#include <arch/x86/x86intrin.h>
#include <fbl/algorithm.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

enum entropy_instr {
    ENTROPY_INSTR_RDSEED,
    ENTROPY_INSTR_RDRAND,
};
static ssize_t get_entropy_from_instruction(void* buf, size_t len, bool block,
                                            enum entropy_instr instr);
static ssize_t get_entropy_from_rdseed(void* buf, size_t len, bool block);
static ssize_t get_entropy_from_rdrand(void* buf, size_t len, bool block);

/* @brief Get entropy from the CPU using RDSEED.
 *
 * len must be at most SSIZE_MAX
 *
 * If |block|=true, it will retry the RDSEED instruction until |len| bytes are
 * written to |buf|.  Otherwise, it will fetch data from RDSEED until either
 * |len| bytes are written to |buf| or RDSEED is unable to return entropy.
 *
 * Returns the number of bytes written to the buffer on success (potentially 0),
 * and a negative value on error.
 */
static ssize_t get_entropy_from_cpu(void* buf, size_t len, bool block) {
    /* TODO(security, ZX-984): Move this to a shared kernel/user lib, so we can write usermode
     * tests against this code */

    if (len >= SSIZE_MAX) {
        static_assert(ZX_ERR_INVALID_ARGS < 0, "");
        return ZX_ERR_INVALID_ARGS;
    }

    if (x86_feature_test(X86_FEATURE_RDSEED)) {
        return get_entropy_from_rdseed(buf, len, block);
    } else if (x86_feature_test(X86_FEATURE_RDRAND)) {
        return get_entropy_from_rdrand(buf, len, block);
    }

    /* We don't have an entropy source */
    static_assert(ZX_ERR_NOT_SUPPORTED < 0, "");
    return ZX_ERR_NOT_SUPPORTED;
}

__attribute__((target("rdrnd,rdseed"))) static bool instruction_step(enum entropy_instr instr,
                                                                     unsigned long long int* val) {
    switch (instr) {
    case ENTROPY_INSTR_RDRAND:
        return _rdrand64_step(val);
    case ENTROPY_INSTR_RDSEED:
        return _rdseed64_step(val);
    default:
        panic("Invalid entropy instruction %d\n", (int)instr);
    }
}

static ssize_t get_entropy_from_instruction(void* buf, size_t len, bool block,
                                            enum entropy_instr instr) {

    size_t written = 0;
    while (written < len) {
        unsigned long long int val = 0;
        if (!instruction_step(instr, &val)) {
            if (!block) {
                break;
            }
            continue;
        }
        const size_t to_copy = fbl::min(len - written, sizeof(val));
        memcpy(static_cast<uint8_t*>(buf) + written, &val, to_copy);
        written += to_copy;
    }
    if (block) {
        DEBUG_ASSERT(written == len);
    }
    return (ssize_t)written;
}

static ssize_t get_entropy_from_rdseed(void* buf, size_t len, bool block) {
    return get_entropy_from_instruction(buf, len, block, ENTROPY_INSTR_RDSEED);
}

static ssize_t get_entropy_from_rdrand(void* buf, size_t len, bool block) {
    // TODO(security, ZX-983): This method is not compliant with Intel's "Digital Random
    // Number Generator (DRNG) Software Implementation Guide".  We are using
    // rdrand in a way that is explicitly against their recommendations.  This
    // needs to be corrected, but this fallback is a compromise to allow our
    // development platforms that don't support RDSEED to get some degree of
    // hardware-based randomization.
    return get_entropy_from_instruction(buf, len, block, ENTROPY_INSTR_RDRAND);
}

size_t hw_rng_get_entropy(void* buf, size_t len, bool block) {
    if (!len) {
        return 0;
    }

    ssize_t res = get_entropy_from_cpu(buf, len, block);
    if (res < 0) {
        return 0;
    }
    return (size_t)res;
}
