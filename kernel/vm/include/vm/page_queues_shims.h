// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_SHIMS_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_SHIMS_H_

#include <kernel/deadline.h>
#include <kernel/event.h>
#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// The 2018 tree defines ZX_TIME_INFINITE but not ZX_TIME_INFINITE_PAST, which
// upstream uses to initialize last_age_time_. It is simply the earliest
// representable monotonic time (0 ns). Provide it here so page_queues.h resolves
// its member initializer in every translation unit that includes it.
#ifndef ZX_TIME_INFINITE_PAST
#define ZX_TIME_INFINITE_PAST ((zx_time_t)0)
#endif

// Compatibility shims that back the F7 PageQueues on 2018 kernel primitives.
//
// F7 uses several primitives that the 2018 kernel lacks under the same names:
//   * CriticalMutex (a preemption-disabling mutex) with the lockdep-style
//     DECLARE_CRITICAL_MUTEX / Lock<CriticalMutex> / Guard<CriticalMutex> spelling.
//   * AutounsignalEvent (an auto-reset event with a WaitDeadline API).
//   * A counting Semaphore with Post()/Wait(Deadline)/count().
//   * The scoped enum Interruptible used with WaitDeadline.
//
// The 2018 tree provides fbl::Mutex (via lockdep's Guard<Mutex>/DECLARE_MUTEX),
// the C event_t API (event_init/event_wait_deadline/event_signal_etc/...), and a
// wait_queue. This header maps the upstream spellings onto those, keeping the F7
// call sites unchanged in shape. Lock ordering / preemption disabling from the
// real CriticalMutex is not modeled; here it is a plain fbl::Mutex.

// The 2018 kernel has no scoped Interruptible enum; event_wait_deadline takes a
// bool. Provide the enum so F7 WaitDeadline call sites compile, and translate it
// in AutounsignalEvent::WaitDeadline below.
enum class Interruptible : bool { No = false, Yes = true };

// CriticalMutex maps to the plain 2018 fbl::Mutex. The real CriticalMutex also
// disables preemption while held; that behavior is not needed for this inert
// stage and is intentionally not modeled. The DECLARE_CRITICAL_MUTEX macro,
// like DECLARE_MUTEX, just declares an fbl::Mutex member (the lockdep container
// tag is ignored).
using CriticalMutex = ::fbl::Mutex;
#define DECLARE_CRITICAL_MUTEX(container) ::fbl::Mutex

// Upstream refers to the lock type as Lock<CriticalMutex>. The 2018 lockdep shim
// has no Lock<> template; get_lock() returns the underlying fbl::Mutex directly,
// which is what Guard<CriticalMutex> (== Guard<fbl::Mutex>) consumes.
template <typename LockType>
using Lock = LockType;

// AutounsignalEvent: an auto-reset (EVENT_FLAG_AUTOUNSIGNAL) event backed by the
// 2018 C event_t API. Supports the F7 usages: default construction, construction
// with an initial signalled state (AutounsignalEvent(true) for aging_token_),
// Wait()/WaitDeadline()/Signal().
class AutounsignalEvent {
public:
    explicit AutounsignalEvent(bool initial = false) {
        event_init(&event_, initial, EVENT_FLAG_AUTOUNSIGNAL);
    }
    ~AutounsignalEvent() { event_destroy(&event_); }

    AutounsignalEvent(const AutounsignalEvent&) = delete;
    AutounsignalEvent& operator=(const AutounsignalEvent&) = delete;

    // Blocking wait, uninterruptible, until the event is signalled.
    zx_status_t Wait() { return event_wait_deadline(&event_, ZX_TIME_INFINITE, false); }

    // Wait until |deadline| or until signalled. |interruptible| selects whether the
    // wait can be interrupted by thread signals; the 2018 event API takes a bool.
    zx_status_t WaitDeadline(zx_time_t deadline, Interruptible interruptible) {
        return event_wait_deadline(&event_, deadline,
                                   interruptible == Interruptible::Yes);
    }

    void Signal() { event_signal_etc(&event_, true, ZX_OK); }

private:
    event_t event_;
};

// Semaphore: a counting semaphore providing the F7 surface Post()/Wait(Deadline)/
// count(). The 2018 object/Semaphore lives in the object module (which depends on
// vm, not the reverse), so to avoid a dependency cycle this is a small
// self-contained implementation over an atomic counter plus an event_t used to
// wake waiters. Wait() decrements when a permit is available, otherwise blocks
// until Post() signals. This is sufficient for the page queues, where the
// semaphore is posted by the lru thread and waited on by the aging thread.
class Semaphore {
public:
    explicit Semaphore(int64_t initial_count = 0) : count_(initial_count) {
        event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
    }
    ~Semaphore() { event_destroy(&event_); }

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    void Post() {
        count_.fetch_add(1, ktl::memory_order_seq_cst);
        event_signal_etc(&event_, true, ZX_OK);
    }

    // Attempts to acquire a permit, blocking until |deadline|. Returns ZX_OK on
    // success or ZX_ERR_TIMED_OUT if the deadline passes first.
    zx_status_t Wait(const Deadline& deadline) {
        while (true) {
            int64_t observed = count_.load(ktl::memory_order_seq_cst);
            while (observed > 0) {
                if (count_.compare_exchange_weak(&observed, observed - 1,
                                                 ktl::memory_order_seq_cst,
                                                 ktl::memory_order_seq_cst)) {
                    return ZX_OK;
                }
            }
            zx_status_t status = event_wait_deadline(&event_, deadline.when(), false);
            if (status == ZX_ERR_TIMED_OUT) {
                return status;
            }
        }
    }

    uint64_t count() const {
        int64_t value = count_.load(ktl::memory_order_seq_cst);
        return value < 0 ? 0 : static_cast<uint64_t>(value);
    }

private:
    ktl::atomic<int64_t> count_;
    event_t event_;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUES_SHIMS_H_
