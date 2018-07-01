// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <debug.h>
#include <zircon/compiler.h>

#if defined(WITH_LIB_CONSOLE)
#include <lib/console.h>

#include <assert.h>
#include <err.h>
#include <platform.h>
#include <lib/unittest/unittest.h>

STATIC_COMMAND_START
STATIC_COMMAND("thread_tests", "test the scheduler", &thread_tests)
STATIC_COMMAND("clock_tests", "test clocks", &clock_tests)
STATIC_COMMAND("sleep_tests", "tests sleep", &sleep_tests)
STATIC_COMMAND("bench", "miscellaneous benchmarks", &benchmarks)
STATIC_COMMAND("fibo", "threaded fibonacci", &fibo)
STATIC_COMMAND("spinner", "create a spinning thread", &spinner)
STATIC_COMMAND("timer_tests", "tests timers", &timer_tests)
STATIC_COMMAND("uart_tests", "tests uart Tx", &uart_tests)
STATIC_COMMAND_END(tests);

#endif
