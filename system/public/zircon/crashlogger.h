// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

// Special values we put in the first register to let crashlogger know that we
// are only requesting a backtrace and that it should resume the thread.  Which
// value is used says how much of the process to print.
#define CRASHLOGGER_REQUEST_SELF_BT_ALL_THREADS_MAGIC 0xee726573756d65eeull
#define CRASHLOGGER_REQUEST_SELF_BT_CURRENT_THREAD_MAGIC 0xee726573756d65efull

// The name this value had before there was more than one of them.
#define CRASHLOGGER_REQUEST_SELF_BT_MAGIC CRASHLOGGER_REQUEST_SELF_BT_ALL_THREADS_MAGIC

// These are always inlined so that the breakpoint lands in the caller's frame.  Left to itself the
// compiler emits them out of line and tail calls between them, so the trap runs in a frameless leaf
// and nothing on the stack records the path that led there.  The analyzer then recovers a caller
// that is not one, and the requesting thread's backtrace stops there.  Inlining puts the trap in
// the frame of whoever asked for the backtrace, which is also how upstream declares these.

__ALWAYS_INLINE static inline void crashlogger_software_break(uint64_t magic) {
#ifdef __x86_64__
    __asm__ volatile ("int3" : : "a" (magic));
#endif
#ifdef __aarch64__
    // This is what gdb uses.
    __asm__ volatile ("mov x0, %0\n"
                      "\tbrk 0"
                      : : "r" (magic) : "x0");
#endif
}

// Invoke this function to cause crashlogger to print a backtrace of the
// calling thread and resume it without killing the process.

__ALWAYS_INLINE static inline void crashlogger_request_backtrace(void) {
    crashlogger_software_break(CRASHLOGGER_REQUEST_SELF_BT_CURRENT_THREAD_MAGIC);
}

// Invoke this function to cause crashlogger to print a backtrace of every
// thread of the process and resume the calling thread without killing the
// process.  The threads that did not ask are the point: they are what a
// wedged process has to be diagnosed from.

__ALWAYS_INLINE static inline void crashlogger_request_backtrace_all_threads(void) {
    crashlogger_software_break(CRASHLOGGER_REQUEST_SELF_BT_ALL_THREADS_MAGIC);
}
