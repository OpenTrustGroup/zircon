// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <lib/unittest/unittest.h>
#include <platform.h>

#include <kernel/cpu.h>
#include <kernel/thread.h>
#include <object/semaphore.h>

static bool smoke_test() {
  BEGIN_TEST;

  {
    Semaphore sema;
    ASSERT_EQ(0, sema.count(), "");
    ASSERT_EQ(0, sema.num_waiters(), "");
  }

  {
    Semaphore sema(0);
    ASSERT_EQ(0, sema.count(), "");
    ASSERT_EQ(0, sema.num_waiters(), "");
  }

  {
    Semaphore sema(5);
    ASSERT_EQ(5, sema.count(), "");
    ASSERT_EQ(0, sema.num_waiters(), "");
  }

  {
    constexpr int64_t kPostCount = 10;
    Semaphore sema;

    for (int64_t i = 0; i < kPostCount; ++i) {
      ASSERT_EQ(i, sema.count(), "");
      ASSERT_EQ(0, sema.num_waiters(), "");

      sema.Post();

      ASSERT_EQ(i + 1, sema.count(), "");
      ASSERT_EQ(0, sema.num_waiters(), "");
    }

    for (int64_t i = 0; i < kPostCount; ++i) {
      ASSERT_EQ(kPostCount - i, sema.count(), "");
      ASSERT_EQ(0, sema.num_waiters(), "");

      ASSERT_EQ(ZX_OK, sema.Wait(ZX_TIME_INFINITE, nullptr), "");

      ASSERT_EQ(kPostCount - i - 1, sema.count(), "");
      ASSERT_EQ(0, sema.num_waiters(), "");
    }
  }

  END_TEST;
}

static bool timeout_test() {
  BEGIN_TEST;

  zx_time_t dealine = current_time() + ZX_USEC(10);

  Semaphore sema;
  ASSERT_EQ(0, sema.count(), "");
  ASSERT_EQ(0, sema.num_waiters(), "");
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sema.Wait(dealine, nullptr), "");
  ASSERT_EQ(0, sema.count(), "");
  ASSERT_EQ(0, sema.num_waiters(), "");

  END_TEST;
}

static int wait_sema_thread(void* arg) {
  auto sema = reinterpret_cast<Semaphore*>(arg);
  auto status = sema->Wait(ZX_TIME_INFINITE, nullptr);
  return static_cast<int>(status);
}

static bool thread_is_blocked(const thread_t* t) {
  AutoThreadLock lock;
  return (t->state == THREAD_BLOCKED);
}

static bool thread_is_dead(const thread_t* t) {
  AutoThreadLock lock;
  return (t->state == THREAD_DEATH);
}

enum class Signal { kPost, kKill, kSuspend };

template <Signal signal>
static bool signal_test() {
  BEGIN_TEST;

  Semaphore sema;

  ASSERT_EQ(0, sema.count(), "");
  ASSERT_EQ(0, sema.num_waiters(), "");

  auto thread = thread_create("test semaphore", wait_sema_thread, &sema, DEFAULT_PRIORITY,
                              DEFAULT_STACK_SIZE);

  ASSERT(nullptr != thread);
  thread_resume(thread);

  while (thread_is_blocked(thread) == false) {
    thread_sleep_relative(ZX_MSEC(1));
  }

  ASSERT_EQ(0, sema.count(), "");
  ASSERT_EQ(1, sema.num_waiters(), "");

  int expected_error;
  switch (signal) {
    case Signal::kPost:
      sema.Post();
      expected_error = ZX_OK;
      break;

    case Signal::kKill:
      thread_kill(thread);
      expected_error = ZX_ERR_INTERNAL_INTR_KILLED;
      break;

    case Signal::kSuspend:
      thread_suspend(thread);
      expected_error = ZX_ERR_INTERNAL_INTR_RETRY;
      break;
  }

  int retcode = ZX_OK;
  thread_join(thread, &retcode, ZX_TIME_INFINITE);
  ASSERT_EQ(expected_error, retcode, "");

  ASSERT_EQ(0, sema.count(), "");
  ASSERT_EQ(0, sema.num_waiters(), "");

  END_TEST;
}

static int timed_wait_sema_thread(void* arg) {
  auto sema = reinterpret_cast<Semaphore*>(arg);
  auto status = sema->Wait(current_time() + ZX_MSEC(10), nullptr);
  return static_cast<int>(status);
}

static int bounded_wait_sema_thread(void* arg) {
  auto sema = reinterpret_cast<Semaphore*>(arg);
  auto status = sema->Wait(current_time() + ZX_MSEC(500), nullptr);
  return static_cast<int>(status);
}

// A waiter that times out leaves the wait queue at once but does not run
// again until it is scheduled. A Post in that window has to be kept for the
// next waiter rather than lost. Hold the timed-out waiter off the CPU by
// pinning it beside this thread and spinning instead of sleeping, then check
// that a second waiter receives the Post. The second waiter is pinned there
// too, so that it waits before the timed-out one gets to run. The priorities
// are far enough apart that the scheduler's boost of a woken thread and
// deboost of a spinning one cannot bring them level.
static bool timeout_window_test() {
  BEGIN_TEST;

  Semaphore sema;
  thread_t* self = get_current_thread();
  const cpu_mask_t saved_affinity = self->cpu_affinity;
  const int saved_priority = self->base_priority;
  thread_set_priority(self, HIGH_PRIORITY);
  const cpu_mask_t mask = cpu_num_to_mask(arch_curr_cpu_num());
  thread_set_cpu_affinity(self, mask);

  // The window is only made if this thread sees the timed waiter blocked
  // before its deadline passes. Under emulation this thread can wake too late
  // for that, so try a few times, and fail rather than hang if it never does.
  // The timed waiter runs one level below the default priority: boosted, it
  // reaches at most 19, below this thread's 20 when deboosted while it spins,
  // yet above a busy default-priority thread deboosted to 12, which therefore
  // cannot starve it.
  bool window = false;
  int timed_retcode = ZX_OK;
  for (int attempt = 0; attempt < 10 && !window; attempt++) {
    auto timed = thread_create("timed semaphore", timed_wait_sema_thread, &sema,
                               DEFAULT_PRIORITY - 1, DEFAULT_STACK_SIZE);
    ASSERT(nullptr != timed);
    thread_set_cpu_affinity(timed, mask);
    thread_resume(timed);

    while (!thread_is_blocked(timed) && !thread_is_dead(timed)) {
      thread_sleep_relative(ZX_USEC(100));
    }
    if (thread_is_blocked(timed)) {
      // Spin, so the timed waiter cannot run once its deadline takes it off
      // the wait queue.
      while (thread_is_blocked(timed)) {
      }
      window = true;
      sema.Post();

      auto bounded = thread_create("bounded semaphore", bounded_wait_sema_thread, &sema,
                                   HIGH_PRIORITY, DEFAULT_STACK_SIZE);
      ASSERT(nullptr != bounded);
      thread_set_cpu_affinity(bounded, mask);
      thread_resume(bounded);

      int bounded_retcode = ZX_OK;
      ASSERT_EQ(ZX_OK, thread_join(bounded, &bounded_retcode, current_time() + ZX_SEC(10)), "");
      ASSERT_EQ(ZX_OK, bounded_retcode, "");
    }
    ASSERT_EQ(ZX_OK, thread_join(timed, &timed_retcode, current_time() + ZX_SEC(10)), "");
    ASSERT_EQ(ZX_ERR_TIMED_OUT, timed_retcode, "");
  }
  thread_set_cpu_affinity(self, saved_affinity);
  thread_set_priority(self, saved_priority);

  EXPECT_TRUE(window, "never saw the timed waiter block before its deadline");
  ASSERT_EQ(0, sema.count(), "");
  ASSERT_EQ(0, sema.num_waiters(), "");

  END_TEST;
}

UNITTEST_START_TESTCASE(semaphore_tests)
UNITTEST("smoke_test", smoke_test)
UNITTEST("timeout_test", timeout_test)
UNITTEST("post_signal_test", signal_test<Signal::kPost>)
UNITTEST("kill_signal_test", signal_test<Signal::kKill>)
UNITTEST("suspend_signal_test", signal_test<Signal::kSuspend>)
UNITTEST("timeout_window_test", timeout_window_test)
UNITTEST_END_TESTCASE(semaphore_tests, "semaphore", "Semaphore tests");
