// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

static sync_completion_t completion = SYNC_COMPLETION_INIT;

#define ITERATIONS 64

static int sync_completion_thread_wait(void* arg) {
    for (int iteration = 0u; iteration < ITERATIONS; iteration++) {
        zx_status_t status = sync_completion_wait(&completion, ZX_TIME_INFINITE);
        ASSERT_EQ(status, ZX_OK, "completion wait failed!");
    }

    return 0;
}

static int sync_completion_thread_signal(void* arg) {
    for (int iteration = 0u; iteration < ITERATIONS; iteration++) {
        sync_completion_reset(&completion);
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
        sync_completion_signal(&completion);
    }

    return 0;
}

static bool test_initializer(void) {
    BEGIN_TEST;
    // Let's not accidentally break .bss'd completions
    static sync_completion_t static_completion;
    sync_completion_t completion = SYNC_COMPLETION_INIT;
    int status = memcmp(&static_completion, &completion, sizeof(sync_completion_t));
    EXPECT_EQ(status, 0, "completion's initializer is not all zeroes");
    END_TEST;
}

#define NUM_THREADS 16

static bool test_completions(void) {
    BEGIN_TEST;
    thrd_t signal_thread;
    thrd_t wait_thread[NUM_THREADS];

    for (int idx = 0; idx < NUM_THREADS; idx++)
        thrd_create_with_name(wait_thread + idx, sync_completion_thread_wait, NULL, "completion wait");
    thrd_create_with_name(&signal_thread, sync_completion_thread_signal, NULL, "completion signal");

    for (int idx = 0; idx < NUM_THREADS; idx++)
        thrd_join(wait_thread[idx], NULL);
    thrd_join(signal_thread, NULL);

    END_TEST;
}

static bool test_timeout(void) {
    BEGIN_TEST;
    zx_duration_t timeout = 0u;
    sync_completion_t completion = SYNC_COMPLETION_INIT;
    for (int iteration = 0; iteration < 1000; iteration++) {
        zx_duration_add_duration(timeout, 2000u);
        zx_status_t status = sync_completion_wait(&completion, timeout);
        ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "wait returned spuriously!");
    }
    END_TEST;
}

BEGIN_TEST_CASE(sync_completion_tests)
RUN_TEST(test_initializer)
RUN_TEST(test_completions)
RUN_TEST(test_timeout)
END_TEST_CASE(sync_completion_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
