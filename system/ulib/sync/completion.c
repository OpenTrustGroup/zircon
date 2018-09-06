// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>

#include <limits.h>
#include <stdatomic.h>
#include <zircon/syscalls.h>

enum {
    UNSIGNALED = 0,
    SIGNALED = 1,
};

zx_status_t sync_completion_wait(sync_completion_t* completion, zx_duration_t timeout) {
    zx_time_t deadline =
        (timeout == ZX_TIME_INFINITE) ? ZX_TIME_INFINITE : zx_deadline_after(timeout);
    return sync_completion_wait_deadline(completion, deadline);
}

zx_status_t sync_completion_wait_deadline(sync_completion_t* completion, zx_time_t deadline) {
    // TODO(kulakowski): With a little more state (a waiters count),
    // this could optimistically spin before entering the kernel.

    atomic_int* futex = &completion->futex;

    for (;;) {
        int32_t current_value = atomic_load(futex);
        if (current_value == SIGNALED) {
            return ZX_OK;
        }
        switch (zx_futex_wait(futex, current_value, deadline)) {
        case ZX_OK:
            continue;
        case ZX_ERR_BAD_STATE:
            // If we get ZX_ERR_BAD_STATE, the value of the futex changed between
            // our load and the wait. This could only have happened if we
            // were signaled.
            return ZX_OK;
        case ZX_ERR_TIMED_OUT:
            return ZX_ERR_TIMED_OUT;
        case ZX_ERR_INVALID_ARGS:
        default:
            __builtin_trap();
        }
    }
}

void sync_completion_signal(sync_completion_t* completion) {
    atomic_int* futex = &completion->futex;
    atomic_store(futex, SIGNALED);
    zx_futex_wake(futex, UINT32_MAX);
}

void sync_completion_reset(sync_completion_t* completion) {
    atomic_store(&completion->futex, UNSIGNALED);
}
