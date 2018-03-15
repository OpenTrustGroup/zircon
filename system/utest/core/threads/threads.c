// Copyright 2016 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <unistd.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

#include <unittest/unittest.h>
#include <runtime/thread.h>

#include "register-set.h"
#include "test-threads/threads.h"

static const char kThreadName[] = "test-thread";

static const unsigned kExceptionPortKey = 42u;

static bool get_koid(zx_handle_t handle, zx_koid_t* koid) {
    zx_info_handle_basic_t info;
    size_t records_read;
    ASSERT_EQ(zx_object_get_info(
                  handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                  &records_read, NULL), ZX_OK, "");
    ASSERT_EQ(records_read, 1u, "");
    *koid = info.koid;
    return true;
}

static bool check_reported_pid_and_tid(zx_handle_t thread,
                                       zx_port_packet_t* packet) {
    zx_koid_t pid;
    zx_koid_t tid;
    if (!get_koid(zx_process_self(), &pid))
        return false;
    if (!get_koid(thread, &tid))
        return false;
    EXPECT_EQ(packet->exception.pid, pid, "");
    EXPECT_EQ(packet->exception.tid, tid, "");
    return true;
}

// Suspend the given thread and block until it reaches the suspended state.
static bool suspend_thread_synchronous(zx_handle_t thread) {
    ASSERT_EQ(zx_task_suspend(thread), ZX_OK, "");

    zx_signals_t observed = 0u;
    ASSERT_EQ(zx_object_wait_one(thread, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, &observed), ZX_OK, "");

    return true;
}

// Resume the given thread and block until it reaches the running state.
static bool resume_thread_synchronous(zx_handle_t thread) {
    ASSERT_EQ(zx_task_resume(thread, 0), ZX_OK, "");

    zx_signals_t observed = 0u;
    ASSERT_EQ(zx_object_wait_one(thread, ZX_THREAD_RUNNING, ZX_TIME_INFINITE, &observed), ZX_OK, "");

    return true;
}

static bool wait_thread_exiting(zx_handle_t eport) {
    zx_port_packet_t packet;
    while (true) {
        ASSERT_EQ(zx_port_wait(eport, ZX_TIME_INFINITE, &packet, 0), ZX_OK, "");
        ASSERT_EQ(packet.key, kExceptionPortKey, "");
        ASSERT_EQ(packet.type, (uint32_t)ZX_EXCP_THREAD_EXITING, "");
        break;
    }
    return true;
}

static bool start_thread(zxr_thread_entry_t entry, void* arg,
                         zxr_thread_t* thread_out, zx_handle_t* thread_h) {
    // TODO: Don't leak these when the thread dies.
    const size_t stack_size = 256u << 10;
    zx_handle_t thread_stack_vmo = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_vmo_create(stack_size, 0, &thread_stack_vmo), ZX_OK, "");
    ASSERT_NE(thread_stack_vmo, ZX_HANDLE_INVALID, "");

    uintptr_t stack = 0u;
    ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), 0, thread_stack_vmo, 0, stack_size,
                          ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &stack), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_stack_vmo), ZX_OK, "");

    ASSERT_EQ(zxr_thread_create(zx_process_self(), "test_thread", false,
                                thread_out),
              ZX_OK, "");

    if (thread_h) {
        ASSERT_EQ(zx_handle_duplicate(zxr_thread_get_handle(thread_out), ZX_RIGHT_SAME_RIGHTS,
                                      thread_h), ZX_OK, "");
    }
    ASSERT_EQ(zxr_thread_start(thread_out, stack, stack_size, entry, arg),
              ZX_OK, "");
    return true;
}

static bool start_and_kill_thread(zxr_thread_entry_t entry, void* arg) {
    zxr_thread_t thread;
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(entry, arg, &thread, &thread_h), "");
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK, "");
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL),
              ZX_OK, "");
    zxr_thread_destroy(&thread);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");
    return true;
}

static bool set_debugger_exception_port(zx_handle_t* eport_out) {
    ASSERT_EQ(zx_port_create(0, eport_out), ZX_OK, "");
    zx_handle_t self = zx_process_self();
    ASSERT_EQ(zx_task_bind_exception_port(self, *eport_out, kExceptionPortKey,
                                          ZX_EXCEPTION_PORT_DEBUGGER),
              ZX_OK, "");
    return true;
}

static bool test_basics(void) {
    BEGIN_TEST;
    zxr_thread_t thread;
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)zx_deadline_after(ZX_MSEC(100)),
                             &thread, &thread_h), "");
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");
    END_TEST;
}

static bool test_detach(void) {
    BEGIN_TEST;
    zxr_thread_t thread;
    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0, &event), ZX_OK, "");

    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_wait_detach_fn, &event, &thread, &thread_h), "");
    // We're not detached yet
    ASSERT_FALSE(zxr_thread_detached(&thread), "");

    ASSERT_EQ(zxr_thread_detach(&thread), ZX_OK, "");
    ASSERT_TRUE(zxr_thread_detached(&thread), "");

    // Tell thread to exit
    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK, "");

    // Wait for thread to exit
    ASSERT_EQ(zx_object_wait_one(thread_h,
                                 ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK, "");

    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");

    END_TEST;
}

static bool test_long_name_succeeds(void) {
    BEGIN_TEST;
    // Creating a thread with a super long name should succeed.
    static const char long_name[] =
        "0123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789";
    ASSERT_GT(strlen(long_name), (size_t)ZX_MAX_NAME_LEN-1,
              "too short to truncate");

    zxr_thread_t thread;
    ASSERT_EQ(zxr_thread_create(zx_process_self(), long_name, false, &thread),
              ZX_OK, "");
    zxr_thread_destroy(&thread);
    END_TEST;
}

// zx_thread_start() is not supposed to be usable for creating a
// process's first thread.  That's what zx_process_start() is for.
// Check that zx_thread_start() returns an error in this case.
static bool test_thread_start_on_initial_thread(void) {
    BEGIN_TEST;

    static const char kProcessName[] = "test-proc-thread1";
    zx_handle_t process;
    zx_handle_t vmar;
    zx_handle_t thread;
    ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), ZX_OK, "");
    ASSERT_EQ(zx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread), ZX_OK, "");
    ASSERT_EQ(zx_thread_start(thread, 1, 1, 1, 1), ZX_ERR_BAD_STATE, "");

    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(vmar), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(process), ZX_OK, "");

    END_TEST;
}

// Test that we don't get an assertion failure (and kernel panic) if we
// pass a zero instruction pointer when starting a thread (in this case via
// zx_process_start()).
static bool test_thread_start_with_zero_instruction_pointer(void) {
    BEGIN_TEST;

    static const char kProcessName[] = "test-proc-thread2";
    zx_handle_t process;
    zx_handle_t vmar;
    zx_handle_t thread;
    ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), ZX_OK, "");
    ASSERT_EQ(zx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread), ZX_OK, "");

    REGISTER_CRASH(process);
    ASSERT_EQ(zx_process_start(process, thread, 0, 0, thread, 0), ZX_OK, "");

    zx_signals_t signals;
    EXPECT_EQ(zx_object_wait_one(
        process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals), ZX_OK, "");
    signals &= ZX_TASK_TERMINATED;
    EXPECT_EQ(signals, ZX_TASK_TERMINATED, "");

    ASSERT_EQ(zx_handle_close(process), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(vmar), ZX_OK, "");

    END_TEST;
}

static bool test_kill_busy_thread(void) {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(threads_test_busy_fn, NULL), "");

    END_TEST;
}

static bool test_kill_sleep_thread(void) {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(threads_test_infinite_sleep_fn, NULL), "");

    END_TEST;
}

static bool test_kill_wait_thread(void) {
    BEGIN_TEST;

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0, &event), ZX_OK, "");
    ASSERT_TRUE(start_and_kill_thread(threads_test_infinite_wait_fn, &event), "");
    ASSERT_EQ(zx_handle_close(event), ZX_OK, "");

    END_TEST;
}

static bool test_bad_state_nonstarted_thread(void) {
    BEGIN_TEST;

    // perform a bunch of apis against non started threads (in the INITIAL STATE)
    zx_handle_t thread;

    ASSERT_EQ(zx_thread_create(zx_process_self(), "thread", 5, 0, &thread), ZX_OK, "");
    ASSERT_EQ(zx_task_resume(thread, 0), ZX_ERR_BAD_STATE, "");
    ASSERT_EQ(zx_task_resume(thread, 0), ZX_ERR_BAD_STATE, "");
    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");

    ASSERT_EQ(zx_thread_create(zx_process_self(), "thread", 5, 0, &thread), ZX_OK, "");
    ASSERT_EQ(zx_task_resume(thread, 0), ZX_ERR_BAD_STATE, "");
    ASSERT_EQ(zx_task_suspend(thread), ZX_ERR_BAD_STATE, "");
    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");

    ASSERT_EQ(zx_thread_create(zx_process_self(), "thread", 5, 0, &thread), ZX_OK, "");
    ASSERT_EQ(zx_task_kill(thread), ZX_OK, "");
    ASSERT_EQ(zx_task_kill(thread), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");

    ASSERT_EQ(zx_thread_create(zx_process_self(), "thread", 5, 0, &thread), ZX_OK, "");
    ASSERT_EQ(zx_task_kill(thread), ZX_OK, "");
    ASSERT_EQ(zx_task_resume(thread, 0), ZX_ERR_BAD_STATE, "");
    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");

    ASSERT_EQ(zx_thread_create(zx_process_self(), "thread", 5, 0, &thread), ZX_OK, "");
    ASSERT_EQ(zx_task_kill(thread), ZX_OK, "");
    ASSERT_EQ(zx_task_suspend(thread), ZX_ERR_BAD_STATE, "");
    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");

    END_TEST;
}

// Arguments for self_killing_fn().
struct self_killing_thread_args {
    zxr_thread_t thread; // Used for the thread to kill itself.
    uint32_t test_value; // Used for testing what the thread does.
};

__NO_SAFESTACK static void self_killing_fn(void* arg) {
    struct self_killing_thread_args* args = arg;
    // Kill the current thread.
    zx_task_kill(zxr_thread_get_handle(&args->thread));
    // We should not reach here -- the syscall should not have returned.
    args->test_value = 999;
    zx_thread_exit();
}

// This tests that the zx_task_kill() syscall does not return when a thread
// uses it to kill itself.
static bool test_thread_kills_itself(void) {
    BEGIN_TEST;

    struct self_killing_thread_args args;
    args.test_value = 111;
    zx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(self_killing_fn, &args, &args.thread, &thread_handle), "");
    ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_handle), ZX_OK, "");
    // Check that the thread did not continue execution and modify test_value.
    ASSERT_EQ(args.test_value, 111u, "");
    // We have to destroy the thread afterwards to clean up its internal
    // handle, since it did not properly exit.
    zxr_thread_destroy(&args.thread);

    END_TEST;
}

static bool test_info_task_stats_fails(void) {
    BEGIN_TEST;
    // Spin up a thread.
    zxr_thread_t thread;
    zx_handle_t thandle;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)zx_deadline_after(ZX_MSEC(100)), &thread,
                             &thandle), "");
    ASSERT_EQ(zx_object_wait_one(thandle,
                                 ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK, "");

    // Ensure that task_stats doesn't work on it.
    zx_info_task_stats_t info;
    EXPECT_NE(zx_object_get_info(thandle, ZX_INFO_TASK_STATS,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK,
              "Just added thread support to info_task_status?");
    // If so, replace this with a real test; see example in process.cpp.

    ASSERT_EQ(zx_handle_close(thandle), ZX_OK, "");
    END_TEST;
}

static bool test_resume_suspended(void) {
    BEGIN_TEST;

    zx_handle_t event;
    zxr_thread_t thread;
    zx_handle_t thread_h;

    ASSERT_EQ(zx_event_create(0, &event), ZX_OK, "");
    ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_h), "");
    ASSERT_EQ(zx_task_suspend(thread_h), ZX_OK, "");
    ASSERT_EQ(zx_task_resume(thread_h, 0), ZX_OK, "");

    // The thread should still be blocked on the event when it wakes up
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, zx_deadline_after(ZX_MSEC(100)),
                                 NULL), ZX_ERR_TIMED_OUT, "");

    // Verify thread is blocked (though may still be running if on a very busy system)
    zx_info_thread_t info;
    ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK, "");
    ASSERT_EQ(info.wait_exception_port_type, ZX_EXCEPTION_PORT_TYPE_NONE, "");
    ASSERT_TRUE(info.state == ZX_THREAD_STATE_RUNNING ||
                info.state == ZX_THREAD_STATE_BLOCKED, "");

    // Check that signaling the event while suspended results in the expected
    // behavior
    ASSERT_TRUE(suspend_thread_synchronous(thread_h), "");

    // Verify thread is suspended
    ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK, "");
    ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED, "");
    ASSERT_EQ(info.wait_exception_port_type, ZX_EXCEPTION_PORT_TYPE_NONE, "");

    // Resuming the thread should mark the thread as blocked again.
    ASSERT_TRUE(resume_thread_synchronous(thread_h), "");

    // When a thread has a blocking syscall interrupted for a suspend, it may
    // momentarily resume running.  If we catch it in the intermediate state,
    // give it a chance to quiesce.
    const size_t kNumTries = 20;
    for (size_t i = 0; i < kNumTries; ++i) {
        ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                     &info, sizeof(info), NULL, NULL),
                  ZX_OK, "");

        if (info.state == ZX_THREAD_STATE_BLOCKED) {
            break;
        }
        ASSERT_EQ(info.state, ZX_THREAD_STATE_RUNNING, "");
        zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    }
    ASSERT_EQ(info.state, ZX_THREAD_STATE_BLOCKED, "");

    // When the thread is suspended the signaling should not take effect.
    ASSERT_TRUE(suspend_thread_synchronous(thread_h), "");
    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK, "");
    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, zx_deadline_after(ZX_MSEC(100)), NULL), ZX_ERR_TIMED_OUT, "");

    ASSERT_EQ(zx_task_resume(thread_h, 0), ZX_OK, "");

    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE, NULL), ZX_OK, "");

    ASSERT_EQ(zx_object_wait_one(
        thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK, "");

    ASSERT_EQ(zx_handle_close(event), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");

    END_TEST;
}

static bool test_suspend_sleeping(void) {
    BEGIN_TEST;

    const zx_time_t sleep_deadline = zx_deadline_after(ZX_MSEC(100));
    zxr_thread_t thread;

    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)sleep_deadline, &thread, &thread_h), "");

    zx_nanosleep(sleep_deadline - ZX_MSEC(50));

    // Suspend the thread.
    ASSERT_TRUE(suspend_thread_synchronous(thread_h), "");

    ASSERT_EQ(zx_task_resume(thread_h, 0), ZX_OK, "");

    // Wait for the sleep to finish
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK, "");

    const zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    ASSERT_GE(now, sleep_deadline, "thread did not sleep long enough");

    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");
    END_TEST;
}

static bool test_suspend_channel_call(void) {
    BEGIN_TEST;

    zxr_thread_t thread;

    zx_handle_t channel;
    struct channel_call_suspend_test_arg thread_arg;
    ASSERT_EQ(zx_channel_create(0, &thread_arg.channel, &channel), ZX_OK, "");
    thread_arg.call_status = ZX_ERR_BAD_STATE;

    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_channel_call_fn, &thread_arg, &thread, &thread_h), "");

    // Wait for the thread to send a channel call before suspending it
    ASSERT_EQ(zx_object_wait_one(channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, NULL),
              ZX_OK, "");

    // Suspend the thread.
    ASSERT_TRUE(suspend_thread_synchronous(thread_h), "");

    // Read the message
    uint8_t buf[9];
    uint32_t actual_bytes;
    ASSERT_EQ(zx_channel_read(channel, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL),
              ZX_OK, "");
    ASSERT_EQ(actual_bytes, sizeof(buf), "");
    ASSERT_EQ(memcmp(buf, "abcdefghi", sizeof(buf)), 0, "");

    // Write a reply
    buf[8] = 'j';
    ASSERT_EQ(zx_channel_write(channel, 0, buf, sizeof(buf), NULL, 0), ZX_OK, "");

    // Make sure the remote channel didn't get signaled
    EXPECT_EQ(zx_object_wait_one(thread_arg.channel, ZX_CHANNEL_READABLE, 0, NULL),
              ZX_ERR_TIMED_OUT, "");

    // Make sure we can't read from the remote channel (the message should have
    // been reserved for the other thread, even though it is suspended).
    EXPECT_EQ(zx_channel_read(thread_arg.channel, 0, buf, NULL, sizeof(buf), 0,
                              &actual_bytes, NULL),
              ZX_ERR_SHOULD_WAIT, "");

    // Wake the suspended thread
    ASSERT_EQ(zx_task_resume(thread_h, 0), ZX_OK, "");

    // Wait for the thread to finish
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK, "");
    EXPECT_EQ(thread_arg.call_status, ZX_OK, "");
    EXPECT_EQ(thread_arg.read_status, ZX_OK, "");

    ASSERT_EQ(zx_handle_close(channel), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");

    END_TEST;
}

static bool test_suspend_port_call(void) {
    BEGIN_TEST;

    zxr_thread_t thread;
    zx_handle_t port[2];
    ASSERT_EQ(zx_port_create(0, &port[0]), ZX_OK, "");
    ASSERT_EQ(zx_port_create(0, &port[1]), ZX_OK, "");

    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_port_fn, port, &thread, &thread_h), "");

    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    ASSERT_EQ(zx_task_suspend(thread_h), ZX_OK, "");

    zx_port_packet_t packet1 = { 100ull, ZX_PKT_TYPE_USER, 0u, {} };
    zx_port_packet_t packet2 = { 300ull, ZX_PKT_TYPE_USER, 0u, {} };

    ASSERT_EQ(zx_port_queue(port[0], &packet1, 0u), ZX_OK, "");
    ASSERT_EQ(zx_port_queue(port[0], &packet2, 0u), ZX_OK, "");

    zx_port_packet_t packet;
    ASSERT_EQ(zx_port_wait(port[1], zx_deadline_after(ZX_MSEC(100)), &packet, 0u), ZX_ERR_TIMED_OUT, "");

    ASSERT_EQ(zx_task_resume(thread_h, 0), ZX_OK, "");

    ASSERT_EQ(zx_port_wait(port[1], ZX_TIME_INFINITE, &packet, 0u), ZX_OK, "");
    EXPECT_EQ(packet.key, 105ull, "");

    ASSERT_EQ(zx_port_wait(port[0], ZX_TIME_INFINITE, &packet, 0u), ZX_OK, "");
    EXPECT_EQ(packet.key, 300ull, "");

    ASSERT_EQ(zx_object_wait_one(
        thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK, "");

    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(port[0]), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(port[1]), ZX_OK, "");

    END_TEST;
}

struct test_writing_thread_arg {
    volatile int v;
};

__NO_SAFESTACK static void test_writing_thread_fn(void* arg_) {
    struct test_writing_thread_arg* arg = arg_;
    while (true) {
        arg->v = 1;
    }
    __builtin_trap();
}

static bool test_suspend_stops_thread(void) {
    BEGIN_TEST;

    zxr_thread_t thread;

    struct test_writing_thread_arg arg = { .v = 0 };
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(test_writing_thread_fn, &arg, &thread, &thread_h), "");

    while (arg.v != 1) {
        zx_nanosleep(0);
    }
    ASSERT_EQ(zx_task_suspend(thread_h), ZX_OK, "");
    while (arg.v != 2) {
        arg.v = 2;
        // Give the thread a chance to clobber the value
        zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
    }
    ASSERT_EQ(zx_task_resume(thread_h, 0), ZX_OK, "");
    while (arg.v != 1) {
        zx_nanosleep(0);
    }

    // Clean up.
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK, "");
    // Wait for the thread termination to complete.  We should do this so
    // that any later tests which use set_debugger_exception_port() do not
    // receive an ZX_EXCP_THREAD_EXITING event.
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");

    END_TEST;
}

// This tests for a bug in which killing a suspended thread causes the
// thread to be resumed and execute more instructions in userland.
static bool test_kill_suspended_thread(void) {
    BEGIN_TEST;

    zxr_thread_t thread;
    struct test_writing_thread_arg arg = { .v = 0 };
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(test_writing_thread_fn, &arg, &thread, &thread_h), "");

    // Wait until the thread has started and has modified arg.v.
    while (arg.v != 1) {
        zx_nanosleep(0);
    }


    ASSERT_TRUE(suspend_thread_synchronous(thread_h), "");

    // Attach to debugger port so we can see ZX_EXCP_THREAD_EXITING.
    zx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport),"");

    // Reset the test memory location.
    arg.v = 100;
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK, "");
    // Wait for the thread termination to complete.
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL), ZX_OK, "");
    // Check for the bug.  The thread should not have resumed execution and
    // so should not have modified arg.v.
    EXPECT_EQ(arg.v, 100, "");

    // Check that the thread is reported as exiting and not as resumed.
    ASSERT_TRUE(wait_thread_exiting(eport), "");

    // Clean up.
    ASSERT_EQ(zx_handle_close(eport), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");

    END_TEST;
}

static bool port_wait_for_signal_once(zx_handle_t port, zx_handle_t thread,
                                      zx_time_t deadline, zx_signals_t mask,
                                      zx_port_packet_t* packet) {
    ASSERT_EQ(zx_object_wait_async(thread, port, 0u, mask,
                                   ZX_WAIT_ASYNC_ONCE),
              ZX_OK, "");
    ASSERT_EQ(zx_port_wait(port, deadline, packet, 1), ZX_OK, "");
    ASSERT_EQ(packet->type, ZX_PKT_TYPE_SIGNAL_ONE, "");
    return true;
}

static bool port_wait_for_signal_repeating(zx_handle_t port,
                                           zx_time_t deadline,
                                           zx_port_packet_t* packet) {
    ASSERT_EQ(zx_port_wait(port, deadline, packet, 1), ZX_OK, "");
    ASSERT_EQ(packet->type, ZX_PKT_TYPE_SIGNAL_REP, "");
    return true;
}

// Test signal delivery of suspended threads via async wait.
static bool test_suspend_wait_async_signal_delivery_worker(bool use_repeating) {
    zx_handle_t event;
    zx_handle_t port;
    zxr_thread_t thread;
    zx_handle_t thread_h;
    const zx_signals_t run_susp_mask = ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED;

    ASSERT_EQ(zx_event_create(0, &event), ZX_OK, "");
    ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_h), "");

    ASSERT_EQ(zx_port_create(0, &port), ZX_OK, "");
    if (use_repeating) {
        ASSERT_EQ(zx_object_wait_async(thread_h, port, 0u, run_susp_mask,
                                       ZX_WAIT_ASYNC_REPEATING),
                  ZX_OK, "");
    }

    zx_port_packet_t packet;
    // There should be a RUNNING signal packet present and not SUSPENDED.
    // This is from when the thread first started to run.
    if (use_repeating) {
        ASSERT_TRUE(port_wait_for_signal_repeating(port, 0u, &packet), "");
    } else {
        ASSERT_TRUE(port_wait_for_signal_once(port, thread_h, 0u, run_susp_mask, &packet), "");
    }
    ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING, "");

    // Make sure there are no more packets.
    if (use_repeating) {
        ASSERT_EQ(zx_port_wait(port, 0u, &packet, 1), ZX_ERR_TIMED_OUT, "");
    } else {
        // In the non-repeating case we have to do things differently as one of
        // RUNNING or SUSPENDED is always asserted.
        ASSERT_EQ(zx_object_wait_async(thread_h, port, 0u,
                                       ZX_THREAD_SUSPENDED,
                                       ZX_WAIT_ASYNC_ONCE),
                  ZX_OK, "");
        ASSERT_EQ(zx_port_wait(port, 0u, &packet, 1), ZX_ERR_TIMED_OUT, "");
        ASSERT_EQ(zx_port_cancel(port, thread_h, 0u), ZX_OK, "");
    }

    zx_info_thread_t info;
    ASSERT_TRUE(suspend_thread_synchronous(thread_h), "");
    ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK, "");
    ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED, "");
    ASSERT_TRUE(resume_thread_synchronous(thread_h), "");
    ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK, "");
    // At this point the thread may be running or blocked waiting for an
    // event. Either one is fine.
    ASSERT_TRUE(info.state == ZX_THREAD_STATE_RUNNING ||
                info.state == ZX_THREAD_STATE_BLOCKED, "");

    // For repeating async waits we should see both SUSPENDED and RUNNING on
    // the port. And we should see them at the same time (and not one followed
    // by the other).
    if (use_repeating) {
        ASSERT_TRUE(port_wait_for_signal_repeating(port,
                                                   zx_deadline_after(ZX_MSEC(100)),
                                                   &packet), "");
        ASSERT_EQ(packet.signal.observed & run_susp_mask, run_susp_mask, "");
    } else {
        // For non-repeating async waits we should see just RUNNING,
        // and it should be immediately present (no deadline).
        ASSERT_TRUE(port_wait_for_signal_once(port, thread_h, 0u, run_susp_mask,
                                              &packet), "");
        ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING, "");
    }

    // The thread should still be blocked on the event when it wakes up.
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, zx_deadline_after(ZX_MSEC(100)),
                                 NULL), ZX_ERR_TIMED_OUT, "");
    ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK, "");
    ASSERT_TRUE(info.state == ZX_THREAD_STATE_RUNNING ||
                info.state == ZX_THREAD_STATE_BLOCKED, "");

    // Check that suspend/resume while blocked in a syscall results in
    // the expected behavior and is visible via async wait.
    ASSERT_EQ(zx_task_suspend(thread_h), ZX_OK, "");
    if (use_repeating) {
        ASSERT_TRUE(port_wait_for_signal_repeating(port,
                                                   zx_deadline_after(ZX_MSEC(100)),
                                                   &packet), "");
    } else {
        ASSERT_TRUE(port_wait_for_signal_once(port, thread_h,
                                              zx_deadline_after(ZX_MSEC(100)),
                                              ZX_THREAD_SUSPENDED, &packet), "");
    }
    ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_SUSPENDED, "");
    ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK, "");
    ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED, "");
    ASSERT_EQ(zx_task_resume(thread_h, 0), ZX_OK, "");
    if (use_repeating) {
        ASSERT_TRUE(port_wait_for_signal_repeating(port,
                                                   zx_deadline_after(ZX_MSEC(100)),
                                                   &packet), "");
    } else {
        ASSERT_TRUE(port_wait_for_signal_once(port, thread_h,
                                              zx_deadline_after(ZX_MSEC(100)),
                                              ZX_THREAD_RUNNING, &packet), "");
    }
    ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING, "");
    ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK, "");
    // Resumption from being suspended back into a blocking syscall will be
    // in the RUNNING state and then BLOCKED.
    ASSERT_TRUE(info.state == ZX_THREAD_STATE_RUNNING ||
                info.state == ZX_THREAD_STATE_BLOCKED,
                "");

    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK, "");
    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE, NULL), ZX_OK, "");

    ASSERT_EQ(zx_object_wait_one(
        thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK, "");

    ASSERT_EQ(zx_handle_close(port), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(event), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK, "");

    return true;
}

// Test signal delivery of suspended threads via single async wait.
static bool test_suspend_single_wait_async_signal_delivery(void) {
    BEGIN_TEST;
    EXPECT_TRUE(test_suspend_wait_async_signal_delivery_worker(false), "");
    END_TEST;
}

// Test signal delivery of suspended threads via repeating async wait.
static bool test_suspend_repeating_wait_async_signal_delivery(void) {
    BEGIN_TEST;
    EXPECT_TRUE(test_suspend_wait_async_signal_delivery_worker(true), "");
    END_TEST;
}

// This tests the registers reported by zx_thread_read_state() for a
// suspended thread.  It starts a thread which sets all the registers to
// known test values.
static bool test_reading_register_state(void) {
    BEGIN_TEST;

    zx_thread_state_general_regs_t regs_expected;
    regs_fill_test_values(&regs_expected);
    regs_expected.REG_PC = (uintptr_t)spin_with_regs_spin_address;

    zxr_thread_t thread;
    zx_handle_t thread_handle;
    ASSERT_TRUE(start_thread((void (*)(void*))spin_with_regs, &regs_expected,
                             &thread, &thread_handle), "");

    // Allow some time for the thread to begin execution and reach the
    // instruction that spins.
    ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_MSEC(100))), ZX_OK, "");

    ASSERT_TRUE(suspend_thread_synchronous(thread_handle), "");

    zx_thread_state_general_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(regs)), ZX_OK, "");
    ASSERT_TRUE(regs_expect_eq(&regs, &regs_expected), "");

    // Clean up.
    ASSERT_EQ(zx_task_kill(thread_handle), ZX_OK, "");
    // Wait for the thread termination to complete.
    ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL), ZX_OK, "");

    END_TEST;
}

// This tests writing registers using zx_thread_write_state().  After
// setting registers using that syscall, it reads back the registers and
// checks their values.
static bool test_writing_register_state(void) {
    BEGIN_TEST;

    zxr_thread_t thread;
    zx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(threads_test_busy_fn, NULL, &thread,
                             &thread_handle), "");

    // Allow some time for the thread to begin execution and reach the
    // instruction that spins.
    ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_MSEC(100))), ZX_OK, "");

    ASSERT_TRUE(suspend_thread_synchronous(thread_handle), "");

    struct {
        // A small stack that is used for calling zx_thread_exit().
        char stack[1024] __ALIGNED(16);
        zx_thread_state_general_regs_t regs_got;
    } stack;

    zx_thread_state_general_regs_t regs_to_set;
    regs_fill_test_values(&regs_to_set);
    regs_to_set.REG_PC = (uintptr_t)save_regs_and_exit_thread;
    regs_to_set.REG_STACK_PTR = (uintptr_t)(stack.stack + sizeof(stack.stack));
    ASSERT_EQ(zx_thread_write_state(
                  thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                  &regs_to_set, sizeof(regs_to_set)), ZX_OK, "");
    ASSERT_EQ(zx_task_resume(thread_handle, 0), ZX_OK, "");
    ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL), ZX_OK, "");
    EXPECT_TRUE(regs_expect_eq(&regs_to_set, &stack.regs_got), "");

    // Clean up.
    ASSERT_EQ(zx_handle_close(thread_handle), ZX_OK, "");

    END_TEST;
}

#if defined(__x86_64__)

// This is based on code from kernel/ which isn't usable by code in system/.
enum { X86_CPUID_ADDR_WIDTH = 0x80000008 };

static uint32_t x86_linear_address_width(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__("cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(X86_CPUID_ADDR_WIDTH), "c"(0));
    return (eax >> 8) & 0xff;
}

#endif

// Test that zx_thread_write_state() does not allow setting RIP to a
// non-canonical address for a thread that was suspended inside a syscall,
// because if the kernel returns to that address using SYSRET, that can
// cause a fault in kernel mode that is exploitable.  See
// sysret_problem.md.
static bool test_noncanonical_rip_address(void) {
    BEGIN_TEST;

#if defined(__x86_64__)
    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0, &event), ZX_OK, "");
    zxr_thread_t thread;
    zx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_handle), "");

    // Allow some time for the thread to begin execution and block inside
    // the syscall.
    ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_MSEC(100))), ZX_OK, "");

    ASSERT_TRUE(suspend_thread_synchronous(thread_handle), "");

    zx_thread_state_general_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(regs)), ZX_OK, "");

    // Example addresses to test.
    uintptr_t noncanonical_addr =
        ((uintptr_t) 1) << (x86_linear_address_width() - 1);
    uintptr_t canonical_addr = noncanonical_addr - 1;
    uint64_t kKernelAddr = 0xffff800000000000;

    zx_thread_state_general_regs_t regs_modified = regs;

    // This RIP address must be disallowed.
    regs_modified.rip = noncanonical_addr;
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs_modified, sizeof(regs_modified)),
              ZX_ERR_INVALID_ARGS, "");

    regs_modified.rip = canonical_addr;
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs_modified, sizeof(regs_modified)),
              ZX_OK, "");

    // This RIP address does not need to be disallowed, but it is currently
    // disallowed because this simplifies the check and it's not useful to
    // allow this address.
    regs_modified.rip = kKernelAddr;
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs_modified, sizeof(regs_modified)),
              ZX_ERR_INVALID_ARGS, "");

    // Clean up: Restore the original register state.
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs, sizeof(regs)), ZX_OK, "");
    // Allow the child thread to resume and exit.
    ASSERT_EQ(zx_task_resume(thread_handle, 0), ZX_OK, "");
    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK, "");
    // Wait for the child thread to signal that it has continued.
    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE,
                                 NULL), ZX_OK, "");
    // Wait for the child thread to exit.
    ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE,
                                 NULL), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(event), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread_handle), ZX_OK, "");
#endif

    END_TEST;
}

// Test that, on ARM64, userland cannot use zx_thread_write_state() to
// modify flag bits such as I and F (bits 7 and 6), which are the IRQ and
// FIQ interrupt disable flags.  We don't want userland to be able to set
// those flags to 1, since that would disable interrupts.  Also, userland
// should not be able to read these bits.
static bool test_writing_arm_flags_register(void) {
    BEGIN_TEST;

#if defined(__aarch64__)
    struct test_writing_thread_arg arg = { .v = 0 };
    zxr_thread_t thread;
    zx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(test_writing_thread_fn, &arg, &thread,
                             &thread_handle), "");
    // Wait for the thread to start executing and enter its main loop.
    while (arg.v != 1) {
        ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_USEC(1))), ZX_OK, "");
    }
    ASSERT_TRUE(suspend_thread_synchronous(thread_handle), "");

    zx_thread_state_general_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(regs)), ZX_OK, "");

    // Check that zx_thread_read_state() does not report any more flag bits
    // than are readable via userland instructions.
    const uint64_t kUserVisibleFlags = 0xf0000000;
    EXPECT_EQ(regs.cpsr & ~kUserVisibleFlags, 0u, "");

    // Try setting more flag bits.
    uint64_t original_cpsr = regs.cpsr;
    regs.cpsr |= ~kUserVisibleFlags;
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs, sizeof(regs)), ZX_OK, "");

    // Firstly, if we read back the register flag, the extra flag bits
    // should have been ignored and should not be reported as set.
    ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(regs)), ZX_OK, "");
    EXPECT_EQ(regs.cpsr, original_cpsr, "");

    // Secondly, if we resume the thread, we should be able to kill it.  If
    // zx_thread_write_state() set the interrupt disable flags, then if the
    // thread gets scheduled, it will never get interrupted and we will not
    // be able to kill and join the thread.
    arg.v = 0;
    ASSERT_EQ(zx_task_resume(thread_handle, 0), ZX_OK, "");
    // Wait until the thread has actually resumed execution.
    while (arg.v != 1) {
        ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_USEC(1))), ZX_OK, "");
    }
    ASSERT_EQ(zx_task_kill(thread_handle), ZX_OK, "");
    ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL), ZX_OK, "");

    // Clean up.
#endif

    END_TEST;
}

BEGIN_TEST_CASE(threads_tests)
RUN_TEST(test_basics)
RUN_TEST(test_detach)
RUN_TEST(test_long_name_succeeds)
RUN_TEST(test_thread_start_on_initial_thread)
RUN_TEST_ENABLE_CRASH_HANDLER(test_thread_start_with_zero_instruction_pointer)
RUN_TEST(test_kill_busy_thread)
RUN_TEST(test_kill_sleep_thread)
RUN_TEST(test_kill_wait_thread)
RUN_TEST(test_bad_state_nonstarted_thread)
RUN_TEST(test_thread_kills_itself)
RUN_TEST(test_info_task_stats_fails)
RUN_TEST(test_resume_suspended)
RUN_TEST(test_suspend_sleeping)
RUN_TEST(test_suspend_channel_call)
RUN_TEST(test_suspend_port_call)
RUN_TEST(test_suspend_stops_thread)
RUN_TEST(test_kill_suspended_thread)
RUN_TEST(test_suspend_single_wait_async_signal_delivery)
RUN_TEST(test_suspend_repeating_wait_async_signal_delivery)
RUN_TEST(test_reading_register_state)
RUN_TEST(test_writing_register_state)
RUN_TEST(test_noncanonical_rip_address)
RUN_TEST(test_writing_arm_flags_register)
END_TEST_CASE(threads_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
