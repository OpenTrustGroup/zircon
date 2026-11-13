// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>
#include <kernel/thread.h>
#include <zircon/types.h>

// You probably don't want to use this class.
class Semaphore {
public:
    explicit Semaphore(int64_t initial_count = 0);
    ~Semaphore();

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    // Releases one waiting thread if there is one, otherwise increments the
    // count.
    void Post();

    // Takes one count if the count is positive. Otherwise blocks until a Post
    // releases this thread, or until |deadline| passes or the wait is
    // interrupted, in which case the count is left unchanged.
    // Sets |was_blocked| to whether the count was 0, so that this thread
    // joined the wait queue; that includes a wait that returned at once for
    // a past deadline or a pending signal.
    zx_status_t Wait(zx_time_t deadline, bool* was_blocked);

    // Observers for tests.
    int64_t count();
    int num_waiters();

private:
    // Never negative: a thread waiting on the Semaphore is counted by the
    // wait queue, not by a negative count.
    int64_t count_;
    wait_queue_t waitq_;
};
