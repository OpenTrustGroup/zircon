// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include <zircon/syscalls.h>
#include <lib/zx/event.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <unittest/unittest.h>

// If set, will run tests that expect the process to die (usually due to a
// failed assertion).
// TODO(dbort): Turn this on if we ever have real death test support. Until
// then, leave this code here so it continues to compile and is easy to turn on
// in a local client for manual testing.
#define RUN_DEATH_TESTS 0

class DestructionTracker : public fbl::RefCounted<DestructionTracker> {
public:
    explicit DestructionTracker(bool* destroyed)
        : destroyed_(destroyed) {}
    ~DestructionTracker() { *destroyed_ = true; }

private:
    bool* destroyed_;
};

static void* inc_and_dec(void* arg) {
    DestructionTracker* tracker = reinterpret_cast<DestructionTracker*>(arg);
    for (size_t i = 0u; i < 500; ++i) {
        fbl::RefPtr<DestructionTracker> ptr(tracker);
    }
    return nullptr;
}

static bool ref_counted_test() {
    BEGIN_TEST;

    bool destroyed = false;
    {
        fbl::AllocChecker ac;
        fbl::RefPtr<DestructionTracker> ptr =
            fbl::AdoptRef(new (&ac) DestructionTracker(&destroyed));
        EXPECT_TRUE(ac.check());

        EXPECT_FALSE(destroyed, "should not be destroyed");
        void* arg = reinterpret_cast<void*>(ptr.get());

        pthread_t threads[5];
        for (size_t i = 0u; i < fbl::count_of(threads); ++i) {
            int res = pthread_create(&threads[i], NULL, &inc_and_dec, arg);
            ASSERT_LE(0, res, "Failed to create inc_and_dec thread!");
        }

        inc_and_dec(arg);

        for (size_t i = 0u; i < fbl::count_of(threads); ++i)
            pthread_join(threads[i], NULL);

        EXPECT_FALSE(destroyed, "should not be destroyed after inc/dec pairs");
    }
    EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");
    END_TEST;
}

static bool make_ref_counted_test() {
    BEGIN_TEST;

    bool destroyed = false;
    {
        auto ptr = fbl::MakeRefCounted<DestructionTracker>(&destroyed);
        EXPECT_FALSE(destroyed, "should not be destroyed");

        fbl::AllocChecker ac;
        auto ptr2 = fbl::MakeRefCountedChecked<DestructionTracker>(&ac, &destroyed);
        EXPECT_TRUE(ac.check());
    }
    EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");
    END_TEST;
}

static bool wrap_dead_pointer_asserts() {
    BEGIN_TEST;
    if (!RUN_DEATH_TESTS) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }

    bool destroyed = false;
    DestructionTracker* raw = nullptr;
    {
        // Create and adopt a ref-counted object, and let it go out of scope.
        fbl::AllocChecker ac;
        fbl::RefPtr<DestructionTracker> ptr =
            fbl::AdoptRef(new (&ac) DestructionTracker(&destroyed));
        EXPECT_TRUE(ac.check());
        raw = ptr.get();
        EXPECT_FALSE(destroyed);
    }
    EXPECT_TRUE(destroyed);

    // Wrapping the now-destroyed object should trigger an assertion.
    fbl::RefPtr<DestructionTracker> zombie = fbl::WrapRefPtr(raw);
    /* NOT REACHED */
    EXPECT_FALSE(true, "Assertion should have fired");

    END_TEST;
}

static bool extra_release_asserts() {
    BEGIN_TEST;
    if (!RUN_DEATH_TESTS) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }

    // Create and adopt a ref-counted object.
    bool destroyed = false;
    fbl::AllocChecker ac;
    fbl::RefPtr<DestructionTracker> ptr =
        fbl::AdoptRef(new (&ac) DestructionTracker(&destroyed));
    EXPECT_TRUE(ac.check());
    DestructionTracker* raw = ptr.get();

    // Manually release once, which should tell us to delete the object.
    EXPECT_TRUE(raw->Release());
    // (But it's not deleted since we didn't listen to the return value
    // of Release())
    EXPECT_FALSE(destroyed);

    // Manually releasing again should trigger the assertion.
    __UNUSED bool unused = raw->Release();
    /* NOT REACHED */
    EXPECT_FALSE(true, "Assertion should have fired");

    END_TEST;
}

static bool wrap_after_last_release_asserts() {
    BEGIN_TEST;
    if (!RUN_DEATH_TESTS) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }

    // Create and adopt a ref-counted object.
    bool destroyed = false;
    fbl::AllocChecker ac;
    fbl::RefPtr<DestructionTracker> ptr =
        fbl::AdoptRef(new (&ac) DestructionTracker(&destroyed));
    EXPECT_TRUE(ac.check());
    DestructionTracker* raw = ptr.get();

    // Manually release once, which should tell us to delete the object.
    EXPECT_TRUE(raw->Release());
    // (But it's not deleted since we didn't listen to the return value
    // of Release())
    EXPECT_FALSE(destroyed);

    // Adding another ref (by wrapping) should trigger the assertion.
    fbl::RefPtr<DestructionTracker> zombie = fbl::WrapRefPtr(raw);
    /* NOT REACHED */
    EXPECT_FALSE(true, "Assertion should have fired");

    END_TEST;
}

static bool unadopted_add_ref_asserts() {
    BEGIN_TEST;
    if (!RUN_DEATH_TESTS) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }

    // An un-adopted ref-counted object.
    bool destroyed = false;
    DestructionTracker obj(&destroyed);

    // Adding a ref (by wrapping) without adopting first should trigger an
    // assertion.
    fbl::RefPtr<DestructionTracker> unadopted = fbl::WrapRefPtr(&obj);
    /* NOT REACHED */
    EXPECT_FALSE(true, "Assertion should have fired");

    END_TEST;
}

static bool unadopted_release_asserts() {
    BEGIN_TEST;
    if (!RUN_DEATH_TESTS) {
        unittest_printf_critical(" [SKIPPING]");
        return true;
    }

    // An un-adopted ref-counted object.
    bool destroyed = false;
    DestructionTracker obj(&destroyed);

    // Releasing without adopting first should trigger an assertion.
    __UNUSED bool unused = obj.Release();
    /* NOT REACHED */
    EXPECT_FALSE(true, "Assertion should have fired");

    END_TEST;
}

namespace {
class RawUpgradeTester : public fbl::RefCounted<RawUpgradeTester> {
public:
    RawUpgradeTester(fbl::Mutex* mutex, fbl::atomic<bool>* destroying, zx::event* event)
        : mutex_(mutex), destroying_(destroying), destroying_event_(event) {}

    ~RawUpgradeTester() {
        atomic_store(destroying_, true);
        if (destroying_event_)
            destroying_event_->signal(0u, ZX_EVENT_SIGNALED);
        fbl::AutoLock al(mutex_);
    }

private:
    fbl::Mutex* mutex_;
    fbl::atomic<bool>* destroying_;
    zx::event* destroying_event_;
};

void* adopt_and_reset(void* arg) {
    fbl::RefPtr<RawUpgradeTester> rc_client =
        fbl::AdoptRef(reinterpret_cast<RawUpgradeTester*>(arg));
    // The reset() which will call the dtor, which we expect to
    // block because upgrade_fail_test() is holding the mutex.
    rc_client.reset();
    return NULL;
}

} // namespace

static bool upgrade_fail_test() {
    BEGIN_TEST;

    fbl::Mutex mutex;
    fbl::AllocChecker ac;
    fbl::atomic<bool> destroying{false};
    zx::event destroying_event;

    zx_status_t status = zx::event::create(0u, &destroying_event);
    ASSERT_EQ(status, ZX_OK);

    auto raw = new (&ac) RawUpgradeTester(&mutex, &destroying, &destroying_event);
    EXPECT_TRUE(ac.check());

    pthread_t thread;
    {
        fbl::AutoLock al(&mutex);
        int res = pthread_create(&thread, NULL, &adopt_and_reset, raw);
        ASSERT_LE(0, res);
        // Wait until the thread is in the destructor.
        status = destroying_event.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), nullptr);
        EXPECT_EQ(status, ZX_OK);
        EXPECT_TRUE(atomic_load(&destroying));
        // The RawUpgradeTester must be blocked in the destructor, the upgrade will fail.
        auto upgrade1 = fbl::internal::MakeRefPtrUpgradeFromRaw(raw, mutex);
        EXPECT_FALSE(upgrade1);
        // Verify that the previous upgrade attempt did not change the refcount.
        auto upgrade2 = fbl::internal::MakeRefPtrUpgradeFromRaw(raw, mutex);
        EXPECT_FALSE(upgrade2);
    }

    pthread_join(thread, NULL);
    END_TEST;
}

static bool upgrade_success_test() {
    BEGIN_TEST;

    fbl::Mutex mutex;
    fbl::AllocChecker ac;
    fbl::atomic<bool> destroying{false};

    auto ref = fbl::AdoptRef(new (&ac) RawUpgradeTester(&mutex, &destroying, nullptr));
    EXPECT_TRUE(ac.check());
    auto raw = ref.get();

    {
        fbl::AutoLock al(&mutex);
        // RawUpgradeTester is not in the destructor so the upgrade should
        // succeed.
        auto upgrade = fbl::internal::MakeRefPtrUpgradeFromRaw(raw, mutex);
        EXPECT_TRUE(upgrade);
    }

    ref.reset();
    EXPECT_TRUE(atomic_load(&destroying));

    END_TEST;
}

BEGIN_TEST_CASE(ref_counted_tests)
RUN_NAMED_TEST("Ref Counted", ref_counted_test)
RUN_NAMED_TEST("Make Ref Counted", make_ref_counted_test)
RUN_NAMED_TEST("Wrapping dead pointer should assert", wrap_dead_pointer_asserts)
RUN_NAMED_TEST("Extra release should assert", extra_release_asserts)
RUN_NAMED_TEST("Wrapping zero-count pointer should assert",
               wrap_after_last_release_asserts)
RUN_NAMED_TEST("AddRef on unadopted object should assert",
               unadopted_add_ref_asserts)
RUN_NAMED_TEST("Release on unadopted object should assert",
               unadopted_release_asserts)
RUN_NAMED_TEST("Fail to upgrade raw pointer ", upgrade_fail_test)
RUN_NAMED_TEST("Upgrade raw pointer", upgrade_success_test)
END_TEST_CASE(ref_counted_tests);
