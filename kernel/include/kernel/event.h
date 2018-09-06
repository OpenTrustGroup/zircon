// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <kernel/thread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define EVENT_MAGIC (0x65766E74) // "evnt"

typedef struct event {
    int magic;
    bool signaled;
    uint flags;
    wait_queue_t wait;
} event_t;

#define EVENT_FLAG_AUTOUNSIGNAL 1

#define EVENT_INITIAL_VALUE(e, initial, _flags)     \
    {                                               \
        .magic = EVENT_MAGIC,                       \
        .signaled = initial,                        \
        .flags = _flags,                            \
        .wait = WAIT_QUEUE_INITIAL_VALUE((e).wait), \
    }

// Rules for Events:
// - Events may be signaled from interrupt context *but* the reschedule
//   parameter must be false in that case.
// - Events may not be waited upon from interrupt context.
// - Events without FLAG_AUTOUNSIGNAL:
//   - Wake up any waiting threads when signaled.
//   - Continue to do so (no threads will wait) until unsignaled.
// - Events with FLAG_AUTOUNSIGNAL:
//   - If one or more threads are waiting when signaled, one thread will
//     be woken up and return.  The signaled state will not be set.
//   - If no threads are waiting when signaled, the Event will remain
//     in the signaled state until a thread attempts to wait (at which
//     time it will unsignal atomicly and return immediately) or
//     event_unsignal() is called.

void event_init(event_t*, bool initial, uint flags);
void event_destroy(event_t*);

// Wait until deadline
// Interruptable arg allows it to return early with ZX_ERR_INTERNAL_INTR_KILLED if thread
// is signaled for kill.
zx_status_t event_wait_deadline(event_t*, zx_time_t, bool interruptable);

// no deadline, non interruptable version of the above.
static inline zx_status_t event_wait(event_t* e) {
    return event_wait_deadline(e, ZX_TIME_INFINITE, false);
}

// Version of event_wait_deadline that ignores existing signals in
// |signal_mask|. There is no deadline, and the caller must be interruptable.
zx_status_t event_wait_with_mask(event_t*, uint signal_mask);

int event_signal_etc(event_t*, bool reschedule, zx_status_t result);
int event_signal(event_t*, bool reschedule);
int event_signal_thread_locked(event_t*) TA_REQ(thread_lock);
zx_status_t event_unsignal(event_t*);

static inline bool event_initialized(const event_t* e) {
    return e->magic == EVENT_MAGIC;
}

static inline bool event_signaled(const event_t* e) {
    return e->signaled;
}

__END_CDECLS

#ifdef __cplusplus

// C++ wrapper. This should be waited on from only a single thread, but may be
// signaled from many threads (Signal() is thread-safe).
class Event {
public:
    Event(uint32_t opts = 0) {
        event_init(&event_, false, opts);
    }
    ~Event() {
        event_destroy(&event_);
    }

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    // Returns:
    // ZX_OK - signaled
    // ZX_ERR_TIMED_OUT - time out expired
    // ZX_ERR_INTERNAL_INTR_KILLED - thread killed
    // Or the |status| which the caller specified in Event::Signal(status)
    zx_status_t Wait(zx_time_t deadline) {
        return event_wait_deadline(&event_, deadline, true);
    }

    void Signal(zx_status_t status = ZX_OK) {
        event_signal_etc(&event_, true, status);
    }

    zx_status_t Unsignal() {
        return event_unsignal(&event_);
    }

private:
    event_t event_;
};

#endif // __cplusplus
