// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/*
 * Functions for unit tests.  See lib/unittest/include/unittest.h for usage.
 */
#include <assert.h>
#include <debug.h>
#include <err.h>
#include <fbl/auto_call.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vm/vm_aspace.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Ensures unittests are not run concurrently.
static mutex_t lock = MUTEX_INITIAL_VALUE(lock);

/**
 * \brief Function called to dump results
 *
 * This function will call the out_func callback
 */
int unittest_printf(const char* format, ...) {
    int ret = 0;

    va_list argp;
    va_start(argp, format);
    ret = vprintf(format, argp);
    va_end(argp);

    return ret;
}

bool unittest_expect_bytes(const uint8_t* expected,
                           const char* expected_name,
                           const uint8_t* actual,
                           const char* actual_name,
                           size_t len,
                           const char* msg,
                           const char* func,
                           int line,
                           bool expect_eq) {
    if (!memcmp(expected, actual, len) != expect_eq) {

        unittest_printf(UNITTEST_FAIL_TRACEF_FORMAT "%s:\n%s %s %s, but %s!\n",
                        func, line, msg,
                        expected_name,
                        expect_eq ? "does not match" : "matches",
                        actual_name,
                        expect_eq ? "should" : "should not");

        unittest_printf("expected (%s)\n", expected_name);
        hexdump8_very_ex(expected, len, (uint64_t)((addr_t)expected), unittest_printf);
        unittest_printf("actual (%s)\n", actual_name);
        hexdump8_very_ex(actual, len, (uint64_t)((addr_t)actual), unittest_printf);

        return false;
    }
    return true;
}

#include <lib/console.h>

// External references to the testcase registration tables.
extern unittest_testcase_registration_t __start_unittest_testcases[];
extern unittest_testcase_registration_t __stop_unittest_testcases[];

static void usage(const char* progname) {
    printf("Usage:\n"
           "%s <case>\n"
           "  where case is a specific testcase name, or...\n"
           "  all : run all tests\n"
           "  ?   : list tests\n",
           progname);
}

static void list_cases(void) {
    size_t count = 0;
    size_t max_namelen = 0;

    const unittest_testcase_registration_t* testcase;
    for (testcase = __start_unittest_testcases;
         testcase != __stop_unittest_testcases;
         ++testcase) {

        if (testcase->name) {
            size_t namelen = strlen(testcase->name);
            if (max_namelen < namelen)
                max_namelen = namelen;
            count++;
        }
    }

    printf("There %s %zu test case%s available...\n",
           count == 1 ? "is" : "are",
           count,
           count == 1 ? "" : "s");

    for (testcase = __start_unittest_testcases;
         testcase != __stop_unittest_testcases;
         ++testcase) {

        if (testcase->name)
            printf("  %-*s : %s\n",
                   static_cast<int>(max_namelen), testcase->name,
                   testcase->desc ? testcase->desc : "<no description>");
    }
}

static bool run_unittest(const unittest_testcase_registration_t* testcase) {
    size_t max_namelen = 0;
    size_t passed = 0;

    DEBUG_ASSERT(testcase);
    DEBUG_ASSERT(testcase->name);
    DEBUG_ASSERT(!!testcase->tests == !!testcase->test_cnt);

    for (size_t i = 0; i < testcase->test_cnt; ++i) {
        const unittest_registration_t* test = &testcase->tests[i];
        if (test->name) {
            size_t namelen = strlen(test->name);
            if (max_namelen < namelen)
                max_namelen = namelen;
        }
    }

    unittest_printf("%s : Running %zu test%s...\n",
                    testcase->name,
                    testcase->test_cnt,
                    testcase->test_cnt == 1 ? "" : "s");

    zx_time_t testcase_start = current_time();

    for (size_t i = 0; i < testcase->test_cnt; ++i) {
        const unittest_registration_t* test = &testcase->tests[i];

        printf("  %-*s : ",
               static_cast<int>(max_namelen), test->name ? test->name : "");

        zx_time_t test_start = current_time();
        bool good = test->fn ? test->fn() : false;
        zx_duration_t test_runtime = current_time() - test_start;

        if (good) {
            passed++;
        } else {
            printf("  %-*s : ",
                   static_cast<int>(max_namelen), test->name ? test->name : "");
        }

        unittest_printf("%s (%" PRIu64 " nSec)\n",
                        good ? "PASSED" : "FAILED",
                        test_runtime);
    }

    zx_duration_t testcase_runtime = current_time() - testcase_start;

    unittest_printf("%s : %sll tests passed (%zu/%zu) in %" PRIu64 " nSec\n",
                    testcase->name,
                    passed != testcase->test_cnt ? "Not a" : "A",
                    passed, testcase->test_cnt,
                    testcase_runtime);

    return passed == testcase->test_cnt;
}

// Runs the testcase specified by |arg| and returns 1 if test passes.
//
// |arg| is a const unittest_testcase_registration_t*.
static int run_unittest_thread_entry(void* arg) {
    auto* testcase = static_cast<const unittest_testcase_registration_t*>(arg);
    return run_unittest(testcase);
}

// Runs |testcase| in another thread and waits for it to complete.
//
// Returns true if the test passed.
static bool run_testcase_in_thread(const unittest_testcase_registration_t* testcase) {
    fbl::RefPtr<VmAspace> aspace = VmAspace::Create(VmAspace::TYPE_USER, "unittest");
    if (!aspace) {
        unittest_printf("failed to create unittest user aspace\n");
        return false;
    }
    auto destroy_aspace = fbl::MakeAutoCall([&]() {
        zx_status_t status = aspace->Destroy();
        DEBUG_ASSERT(status == ZX_OK);
    });
    thread_t* t = thread_create("unittest", run_unittest_thread_entry,
                                const_cast<void*>(static_cast<const void*>(testcase)),
                                DEFAULT_PRIORITY);
    if (!t) {
        unittest_printf("failed to create unittest thread\n");
        return false;
    }
    aspace->AttachToThread(t);

    thread_resume(t);
    int success = 0;
    zx_status_t status = thread_join(t, &success, ZX_TIME_INFINITE);
    if (status != ZX_OK) {
        unittest_printf("failed to join unittest thread: %d\n", status);
        return false;
    }
    return success;
}

static int run_unittests_locked(int argc, const cmd_args* argv, uint32_t flags) {
    DEBUG_ASSERT(is_mutex_held(&lock));
    if (argc != 2) {
        usage(argv[0].str);
        return 0;
    }

    const char* casename = argv[1].str;

    if (!strcmp(casename, "?")) {
        list_cases();
        return 0;
    }

    bool run_all = !strcmp(casename, "all");
    const unittest_testcase_registration_t* testcase;
    size_t chosen = 0;
    size_t passed = 0;

    const size_t num_tests =
        run_all ? __stop_unittest_testcases - __start_unittest_testcases : 1;
    // Array of names with a NULL sentinel at the end.
    const char** failed_names = static_cast<const char**>(calloc(num_tests + 1, sizeof(char*)));
    const char** fn = failed_names;

    for (testcase = __start_unittest_testcases;
         testcase != __stop_unittest_testcases;
         ++testcase) {

        if (testcase->name) {
            if (run_all || !strcmp(casename, testcase->name)) {
                chosen++;

                if (run_testcase_in_thread(testcase)) {
                    passed++;
                } else {
                    *fn++ = testcase->name;
                }
                printf("\n");

                if (!run_all)
                    break;
            }
        }
    }

    int ret = 0;
    if (!run_all && !chosen) {
        ret = -1;
        unittest_printf("Test case \"%s\" not found!\n", casename);
        list_cases();
    } else {
        unittest_printf("SUMMARY: Ran %d test case%s: %d failed\n",
                        chosen, chosen == 1 ? "" : "s", chosen - passed);
        if (passed < chosen) {
            ret = -1;
            unittest_printf("\nThe following test cases failed:\n");
            for (fn = failed_names; *fn != NULL; fn++) {
                unittest_printf("%s\n", *fn);
            }
        }
    }

    free(failed_names);
    return ret;
}

static int run_unittests(int argc, const cmd_args* argv, uint32_t flags) {
    mutex_acquire(&lock);
    const int ret = run_unittests_locked(argc, argv, flags);
    mutex_release(&lock);
    return ret;
}

STATIC_COMMAND_START
STATIC_COMMAND("ut", "Run unittests", run_unittests)
STATIC_COMMAND_END(unittests);
