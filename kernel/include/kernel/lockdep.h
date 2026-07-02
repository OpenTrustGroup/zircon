// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_LOCKDEP_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_LOCKDEP_H_

#ifdef __cplusplus

#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <zircon/compiler.h>

// The 2018 kernel predates the lockdep framework that newer Fuchsia uses for
// instrumented, ordered locks. This header provides the small subset of that API
// consumed by ported VM and pager code so it compiles unchanged: a scoped Guard,
// the DECLARE_MUTEX declaration macro, and a Mutex alias. Locks are mapped onto
// fbl::Mutex; lock ordering is not instrumented.

// Upstream tags each mutex with a lockdep container type; here the tag is ignored
// and the member is a plain fbl::Mutex.
#define DECLARE_MUTEX(container) ::fbl::Mutex

// Newer code names the lock type used as Guard<Mutex>. The 2018 kernel has no
// global Mutex class, so alias it to fbl::Mutex.
using Mutex = ::fbl::Mutex;

// A scoped RAII lock guard modeled on fbl::AutoLock (a proven 2018
// __TA_SCOPED_CAPABILITY) so Clang thread-safety analysis is satisfied. Only the
// scoped acquire/early-release subset used by the pager is provided; CallUnlocked
// and lock-ordering are intentionally omitted.
template <typename LockType>
class __TA_SCOPED_CAPABILITY Guard {
public:
    explicit Guard(LockType* lock) __TA_ACQUIRE(lock) : lock_(lock) {
        lock_->Acquire();
    }

    ~Guard() __TA_RELEASE() { Release(); }

    // Release the lock early, before the guard goes out of scope.
    void Release() __TA_RELEASE() {
        if (lock_) {
            lock_->Release();
            lock_ = nullptr;
        }
    }

    // Run |callable| with the lock temporarily released, then re-acquire it. Used
    // by pager teardown to call into other objects (which take their own locks)
    // without holding this lock, preserving lock ordering. The lock is held again
    // once this returns, so from a caller's perspective the guarded state stays
    // protected; thread-safety analysis of the release/re-acquire is suppressed
    // because it spans the callable.
    template <typename Callable>
    void CallUnlocked(Callable&& callable) __TA_NO_THREAD_SAFETY_ANALYSIS {
        lock_->Release();
        callable();
        lock_->Acquire();
    }

    DISALLOW_COPY_ASSIGN_AND_MOVE(Guard);

private:
    LockType* lock_;
};

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_LOCKDEP_H_
