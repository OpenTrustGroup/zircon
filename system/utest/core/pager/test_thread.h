// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_PAGER_TEST_THREAD_H_
#define ZIRCON_SYSTEM_UTEST_CORE_PAGER_TEST_THREAD_H_

#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <sync/completion.h>
#include <lib/zx/thread.h>
#include <threads.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

namespace pager_tests {

// Class which executes the specified function on a test thread.
class TestThread {
 public:
  explicit TestThread(fbl::Function<bool()> fn);
  ~TestThread();

  // Starts the test thread's execution.
  bool Start();
  // Block until the test thread successfully terminates.
  bool Wait();
  // Block until the test thread terminates with a validation error.
  bool WaitForFailure();
  // Block until the test thread crashes due to an access to |crash_addr|. The exception report's
  // |synth_code| field should be set to |error_status|.
  bool WaitForCrash(uintptr_t crash_addr, zx_status_t error_status);
  // Block until the test thread is blocked.
  bool WaitForBlocked();
  // Block until the thread terminates.
  bool WaitForTerm() {
    return zx_thread_.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr) == ZX_OK;
  }

  // 2018 has handle-based suspend/resume instead of suspend tokens.
  void SuspendSync();
  void Resume();

  void Run();

 private:
  bool Wait(bool expect_failure, bool expect_crash, uintptr_t crash_addr,
            zx_status_t error_status = ZX_OK);
  void PrintDebugInfo(const zx_exception_report_t& report);

  fbl::Function<bool()> fn_;
  thrd_t thrd_ = 0;
  zx::thread zx_thread_;

  // 2018 uses legacy exception ports instead of exception channels.
  zx_handle_t exception_port_ = ZX_HANDLE_INVALID;

  // Makes sure that everything is set up before starting the actual test function.
  completion_t startup_sync_;
  bool success_ = false;
};

}  // namespace pager_tests

#endif  // ZIRCON_SYSTEM_UTEST_CORE_PAGER_TEST_THREAD_H_
