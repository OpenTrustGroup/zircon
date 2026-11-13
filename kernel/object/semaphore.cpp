// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/semaphore.h>

#include <err.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

Semaphore::Semaphore(int64_t initial_count) : count_(initial_count) {
    DEBUG_ASSERT(initial_count >= 0);
    wait_queue_init(&waitq_);
}

Semaphore::~Semaphore() {
    wait_queue_destroy(&waitq_);
}

void Semaphore::Post() {
    AutoThreadLock lock;

    // There may be waiters or a positive count, never both.
    DEBUG_ASSERT(count_ == 0 || wait_queue_is_empty(&waitq_));

    // Hand the post to a waiter if there is one. Counting it instead, and
    // letting a waiter take it back out of a negative count, loses a wakeup:
    // a waiter that times out leaves the wait queue at once but only gives
    // back its decrement when it next runs, and a Post in between wakes
    // nobody.
    if (wait_queue_wake_one(&waitq_, true, ZX_OK) == 0) {
        ++count_;
    }
}

zx_status_t Semaphore::Wait(zx_time_t deadline, bool* was_blocked) {
    thread_t *current_thread = get_current_thread();

    zx_status_t ret = ZX_OK;
    bool block;

    {
        AutoThreadLock lock;
        DEBUG_ASSERT(count_ == 0 || wait_queue_is_empty(&waitq_));

        // Take a count if there is one. Otherwise sit in the wait queue until
        // a Post releases us. A wait that times out or is interrupted took
        // nothing, so there is nothing to give back.
        block = count_ == 0;
        if (likely(!block)) {
            --count_;
        } else {
            current_thread->interruptable = true;
            ret = wait_queue_block(&waitq_, deadline);
            current_thread->interruptable = false;
        }
    }

    if (was_blocked != nullptr)
        *was_blocked = block;
    return ret;
}

int64_t Semaphore::count() {
    AutoThreadLock lock;
    return count_;
}

int Semaphore::num_waiters() {
    AutoThreadLock lock;
    return waitq_.count;
}
