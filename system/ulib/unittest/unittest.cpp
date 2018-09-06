// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <pretty/hexdump.h>

#include "watchdog.h"

#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED
#include "crash-handler.h"
#include "crash-list.h"
#endif // UNITTEST_CRASH_HANDLER_SUPPORTED

using nsecs_t = uint64_t;

static nsecs_t now() {
#ifdef __Fuchsia__
    return zx_clock_get_monotonic();
#else
    // clock_gettime(CLOCK_MONOTONIC) would be better but may not exist on the host
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) < 0)
        return 0u;
    return tv.tv_sec * 1000000000ull + tv.tv_usec * 1000ull;
#endif
}

/**
 * \brief Default function to dump unit test results
 *
 * \param[in] line is the buffer to dump
 * \param[in] len is the length of the buffer to dump
 * \param[in] arg can be any kind of arguments needed to dump the values
 */
static void default_printf(const char* line, int len, void* arg) {
    fputs(line, stdout);
    fflush(stdout);
}

// Default output function is the printf
static test_output_func out_func = default_printf;
// Buffer the argument to be sent to the output function
static void* out_func_arg = nullptr;

// Controls the behavior of unittest_printf.
// To override, specify v=N on the command line.
int utest_verbosity_level = 0;

// Controls the types of tests which are executed.
// Multiple test types can be "OR-ed" together to
// run a subset of all tests.
test_type_t utest_test_type = static_cast<test_type>(TEST_DEFAULT);

/**
 * \brief Function called to dump results
 *
 * This function will call the out_func callback
 */
void unittest_printf_critical(const char* format, ...) {
    static char print_buffer[PRINT_BUFFER_SIZE];

    va_list argp;
    va_start(argp, format);

    if (out_func) {
        // Format the string
        vsnprintf(print_buffer, PRINT_BUFFER_SIZE, format, argp);
        out_func(print_buffer, PRINT_BUFFER_SIZE, out_func_arg);
    }

    va_end(argp);
}

bool unittest_expect_bytes_eq(const uint8_t* expected, const uint8_t* actual, size_t len,
                              const char* msg) {
    if (memcmp(expected, actual, len)) {
        printf("%s. expected\n", msg);
        hexdump8(expected, len);
        printf("actual\n");
        hexdump8(actual, len);
        return false;
    }
    return true;
}

bool unittest_expect_str_eq(const char* str1_value, const char* str2_value,
                            const char* str1_expr, const char* str2_expr,
                            const char* msg,
                            const char* source_filename, int source_line_num,
                            const char* source_function) {
    if (strcmp(str1_value, str2_value)) {
        unittest_printf_critical(
            UNITTEST_FAIL_TRACEF_FORMAT
            "%s:\n"
            "        Comparison failed: strings not equal:\n"
            "        String 1 expression: %s\n"
            "        String 2 expression: %s\n"
            "        String 1 value: \"%s\"\n"
            "        String 2 value: \"%s\"\n",
            source_filename, source_line_num, source_function,
            msg, str1_expr, str2_expr, str1_value, str2_value);
        return false;
    }
    return true;
}

bool unittest_expect_str_ne(const char* str1_value, const char* str2_value,
                            const char* str1_expr, const char* str2_expr,
                            const char* msg,
                            const char* source_filename, int source_line_num,
                            const char* source_function) {
    if (!strcmp(str1_value, str2_value)) {
        unittest_printf_critical(
            UNITTEST_FAIL_TRACEF_FORMAT
            "%s:\n"
            "        Comparison failed: strings are equal,"
            " but expected different strings:\n"
            "        String 1 expression: %s\n"
            "        String 2 expression: %s\n"
            "        Value of both strings: \"%s\"\n",
            source_filename, source_line_num, source_function,
            msg, str1_expr, str2_expr, str1_value);
        return false;
    }
    return true;
}

bool unittest_expect_str_str(const char* str1_value, const char* str2_value,
                            const char* str1_expr, const char* str2_expr,
                            const char* msg,
                            const char* source_filename, int source_line_num,
                            const char* source_function) {
    if (!strstr(str1_value, str2_value)) {
        unittest_printf_critical(
            UNITTEST_FAIL_TRACEF_FORMAT
            "%s:\n"
            "        Comparison failed: String 1 does not"
            " contain String 2:\n"
            "        String 1 expression: %s\n"
            "        String 2 expression: %s\n"
            "        Value of both strings: \"%s\"\n",
            source_filename, source_line_num, source_function,
            msg, str1_expr, str2_expr, str1_value);
        return false;
    }
    return true;
}

void unittest_set_output_function(test_output_func fun, void* arg) {
    out_func = fun;
    out_func_arg = arg;
}

void unittest_restore_output_function() {
    out_func = default_printf;
    out_func_arg = nullptr;
}

int unittest_set_verbosity_level(int new_level) {
    int out = utest_verbosity_level;
    utest_verbosity_level = new_level;
    return out;
}

#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED
void unittest_register_crash(struct test_info* current_test_info, zx_handle_t handle) {
    crash_list_register(current_test_info->crash_list, handle);
}

bool unittest_run_death_fn(void (*fn_to_run)(void*), void* arg) {
    test_result_t test_result;
    zx_status_t status = run_fn_with_crash_handler(fn_to_run, arg, &test_result);
    return status == ZX_OK && test_result == TEST_CRASHED;
}

bool unittest_run_no_death_fn(void (*fn_to_run)(void*), void* arg) {
    test_result_t test_result;
    zx_status_t status = run_fn_with_crash_handler(fn_to_run, arg, &test_result);
    return status == ZX_OK && test_result != TEST_CRASHED;
}
#endif // UNITTEST_CRASH_HANDLER_SUPPORTED

static void unittest_run_test(const char* name,
                              bool (*test)(),
                              struct test_info** current_test_info,
                              bool* all_success,
                              bool enable_crash_handler) {
    unittest_printf_critical("    %-51s [RUNNING]", name);
    nsecs_t start_time = now();
    test_info test_info = {.all_ok = true, nullptr};
    *current_test_info = &test_info;
    // The crash handler is disabled by default. To enable, the test should
    // be run with RUN_TEST_ENABLE_CRASH_HANDLER.
    if (enable_crash_handler) {
#ifdef UNITTEST_CRASH_HANDLER_SUPPORTED
        test_info.crash_list = crash_list_new();

        test_result_t test_result;
        zx_status_t status =
            run_test_with_crash_handler(test_info.crash_list, test, &test_result);
        if (status != ZX_OK || test_result == TEST_FAILED) {
            test_info.all_ok = false;
        }

        // Check if there were any processes registered to crash but didn't.
        bool missing_crash = crash_list_delete(test_info.crash_list);
        if (missing_crash) {
            // TODO: display which expected crash did not occur.
            UNITTEST_FAIL_TRACEF("Expected crash did not occur\n");
            test_info.all_ok = false;
        }
#else  // UNITTEST_CRASH_HANDLER_SUPPORTED
        UNITTEST_FAIL_TRACEF("Crash tests not supported\n");
        test_info.all_ok = false;
#endif // UNITTEST_CRASH_HANDLER_SUPPORTED
    } else if (!test()) {
        test_info.all_ok = false;
    }

    // Recheck all_ok in case there was a failure in a C++ destructor
    // after the "return" statement in END_TEST.
    if (!test_info.all_ok) {
        *all_success = false;
    }

    nsecs_t end_time = now();
    uint64_t time_taken_ms = (end_time - start_time) / 1000000;
    unittest_printf_critical(" [%s] (%d ms)\n", test_info.all_ok ? "PASSED" : "FAILED",
                             static_cast<int>(time_taken_ms));

    *current_test_info = nullptr;
}

template <typename F>
void run_with_watchdog(test_type_t test_type, const char* name, F fn) {
    if (watchdog_is_enabled()) {
        watchdog_start(test_type, name);
        fn();
        watchdog_cancel();
    } else {
        fn();
    }
}

void unittest_run_named_test(const char* name, bool (*test)(), test_type_t test_type,
                             struct test_info** current_test_info, bool* all_success,
                             bool enable_crash_handler) {
    if (utest_test_type & test_type) {
        run_with_watchdog(test_type, name, [&]() {
            unittest_run_test(name, test, current_test_info, all_success, enable_crash_handler);
        });
    } else {
        unittest_printf_critical("    %-51s [IGNORED]\n", name);
    }
}

void unittest_cancel_timeout(void) {
    watchdog_cancel();
}
