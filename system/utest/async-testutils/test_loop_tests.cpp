// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/array.h>
#include <fbl/function.h>
#include <lib/async-testutils/test_loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace {

// Initializes |wait| to wait on |event| to call |closure| once |trigger| is
// signaled.
void InitWait(async::Wait* wait, fbl::Closure closure, const zx::event& event,
              zx_signals_t trigger) {
    wait->set_handler(
        [closure = fbl::move(closure)] (async_dispatcher_t*, async::Wait*,
                                        zx_status_t,
                                        const zx_packet_signal_t*) {
        closure();
    });
    wait->set_object(event.get());
    wait->set_trigger(trigger);
}


bool DefaultDispatcherIsSetAndUnset() {
    BEGIN_TEST;

    EXPECT_NULL(async_get_default_dispatcher());
    {
        async::TestLoop loop;;
        EXPECT_EQ(loop.dispatcher(), async_get_default_dispatcher());
    }
    EXPECT_NULL(async_get_default_dispatcher());

    END_TEST;
}

bool FakeClockTimeIsCorrect() {
    BEGIN_TEST;

    async::TestLoop loop;

    EXPECT_EQ(0, loop.Now().get());
    EXPECT_EQ(0, async::Now(loop.dispatcher()).get());

    loop.RunUntilIdle();
    EXPECT_EQ(0, loop.Now().get());
    EXPECT_EQ(0, async::Now(loop.dispatcher()).get());

    loop.RunFor(zx::nsec(1));
    EXPECT_EQ(1, loop.Now().get());
    EXPECT_EQ(1, async::Now(loop.dispatcher()).get());

    loop.RunUntil(zx::time() + zx::nsec(3));
    EXPECT_EQ(3, loop.Now().get());
    EXPECT_EQ(3, async::Now(loop.dispatcher()).get());

    loop.RunFor(zx::nsec(7));
    EXPECT_EQ(10, loop.Now().get());
    EXPECT_EQ(10, async::Now(loop.dispatcher()).get());

    loop.RunUntil(zx::time() + zx::nsec(12));
    EXPECT_EQ(12, loop.Now().get());
    EXPECT_EQ(12, async::Now(loop.dispatcher()).get());

    // t = 12, so nothing should happen in trying to reset the clock to t = 10.
    loop.RunUntil(zx::time() + zx::nsec(10));
    EXPECT_EQ(12, loop.Now().get());
    EXPECT_EQ(12, async::Now(loop.dispatcher()).get());

    END_TEST;
}

bool TasksAreDispatched() {
    BEGIN_TEST;

    async::TestLoop loop;
    bool called = false;
    async::PostDelayedTask(loop.dispatcher(), [&called] { called = true; }, zx::sec(2));

    // t = 1: nothing should happen.
    loop.RunFor(zx::sec(1));
    EXPECT_FALSE(called);

    // t = 2: task should be dispatched.
    loop.RunFor(zx::sec(1));
    EXPECT_TRUE(called);

    called = false;
    async::PostTask(loop.dispatcher(), [&called] { called = true; });
    loop.RunUntilIdle();
    EXPECT_TRUE(called);

    END_TEST;
}

bool SameDeadlinesDispatchInPostingOrder() {
    BEGIN_TEST;

    async::TestLoop loop;
    bool calledA = false;
    bool calledB = false;

    async::PostTask(loop.dispatcher(), [&] {
        EXPECT_FALSE(calledB);
        calledA = true;
    });
    async::PostTask(loop.dispatcher(), [&] {
      EXPECT_TRUE(calledA);
      calledB = true;
    });

    loop.RunUntilIdle();
    EXPECT_TRUE(calledA);
    EXPECT_TRUE(calledB);

    calledA = false;
    calledB = false;
    async::PostDelayedTask(
        loop.dispatcher(),
        [&] {
            EXPECT_FALSE(calledB);
            calledA = true;
        },
        zx::sec(5));
    async::PostDelayedTask(
        loop.dispatcher(),
        [&] {
            EXPECT_TRUE(calledA);
            calledB = true;
        },
        zx::sec(5));

    loop.RunFor(zx::sec(5));
    EXPECT_TRUE(calledA);
    EXPECT_TRUE(calledB);

    END_TEST;
}

// Test tasks that post tasks.
bool NestedTasksAreDispatched() {
    BEGIN_TEST;

    async::TestLoop loop;
    bool called = false;

    async::PostTask(loop.dispatcher(), [&] {
        async::PostDelayedTask(
            loop.dispatcher(),
            [&] {
                async::PostDelayedTask(
                      loop.dispatcher(),
                      [&] { called = true; },
                      zx::min(25));
            },
            zx::min(35));
    });

    loop.RunFor(zx::hour(1));
    EXPECT_TRUE(called);

    END_TEST;
}

bool TimeIsCorrectWhileDispatching() {
    BEGIN_TEST;

    async::TestLoop loop;
    bool called = false;

    async::PostTask(loop.dispatcher(), [&] {
        EXPECT_EQ(0, loop.Now().get());

        async::PostDelayedTask(
            loop.dispatcher(),
            [&] {
                EXPECT_EQ(10, loop.Now().get());
                async::PostDelayedTask(
                      loop.dispatcher(),
                      [&] {
                          EXPECT_EQ(15, loop.Now().get());
                          async::PostTask(loop.dispatcher(), [&] {
                              EXPECT_EQ(15, loop.Now().get());
                              called = true;
                          });
                      },
                      zx::nsec(5));
            },
            zx::nsec(10));
    });

    loop.RunFor(zx::nsec(15));
    EXPECT_TRUE(called);

    END_TEST;
}

bool TasksAreCanceled() {
    BEGIN_TEST;

    async::TestLoop loop;
    bool calledA = false;
    bool calledB = false;
    bool calledC = false;

    async::TaskClosure taskA([&calledA] { calledA = true; });
    async::TaskClosure taskB([&calledB] { calledB = true; });
    async::TaskClosure taskC([&calledC] { calledC = true; });

    ASSERT_EQ(ZX_OK, taskA.Post(loop.dispatcher()));
    ASSERT_EQ(ZX_OK, taskB.Post(loop.dispatcher()));
    ASSERT_EQ(ZX_OK, taskC.Post(loop.dispatcher()));

    ASSERT_EQ(ZX_OK, taskA.Cancel());
    ASSERT_EQ(ZX_OK, taskC.Cancel());

    loop.RunUntilIdle();

    EXPECT_FALSE(calledA);
    EXPECT_TRUE(calledB);
    EXPECT_FALSE(calledC);

    END_TEST;
}

bool TimeIsAdvanced() {
    BEGIN_TEST;
    async::TestLoop loop;

    bool called = false;
    async::TaskClosure task([&called] { called = true; });
    auto time1 = async::Now(loop.dispatcher());

    ASSERT_EQ(ZX_OK, task.PostDelayed(loop.dispatcher(), zx::duration(1)));

    loop.RunUntilIdle();

    EXPECT_FALSE(called);
    EXPECT_EQ(time1.get(), async::Now(loop.dispatcher()).get());

    loop.AdvanceTimeByEpsilon();

    auto time2 = async::Now(loop.dispatcher());

    EXPECT_FALSE(called);
    EXPECT_GT(time2.get(), time1.get());

    loop.RunUntilIdle();

    EXPECT_TRUE(called);
    EXPECT_EQ(time2.get(), async::Now(loop.dispatcher()).get());

    END_TEST;
}

bool WaitsAreDispatched() {
    BEGIN_TEST;

    async::TestLoop loop;
    async::Wait wait;
    zx::event event;
    bool called = false;

    ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));
    InitWait(&wait, [&called] { called = true; }, event, ZX_USER_SIGNAL_0);
    ASSERT_EQ(ZX_OK, wait.Begin(loop.dispatcher()));

    // |wait| has not yet been triggered.
    loop.RunUntilIdle();
    EXPECT_FALSE(called);

    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_1));

    // |wait| will only be triggered by |ZX_USER_SIGNAL_0|.
    loop.RunUntilIdle();
    EXPECT_FALSE(called);

    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

    loop.RunUntilIdle();
    EXPECT_TRUE(called);

    END_TEST;
}

// Test waits that trigger waits.
bool NestedWaitsAreDispatched() {
    BEGIN_TEST;

    async::TestLoop loop;
    zx::event event;
    async::Wait waitA;
    async::Wait waitB;
    async::Wait waitC;
    bool calledA = false;
    bool calledB = false;
    bool calledC = false;

    ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));
    InitWait(
        &waitA,
        [&] {
            InitWait(
                &waitB,
                [&] {
                    InitWait(&waitC, [&] { calledC = true; }, event, ZX_USER_SIGNAL_2);
                    waitC.Begin(loop.dispatcher());
                    calledB = true;
                },
                event,
                ZX_USER_SIGNAL_1);
            waitB.Begin(loop.dispatcher());
            calledA = true;
        },
        event,
        ZX_USER_SIGNAL_0);

    ASSERT_EQ(ZX_OK, waitA.Begin(loop.dispatcher()));

    loop.RunUntilIdle();
    EXPECT_FALSE(calledA);
    EXPECT_FALSE(calledB);
    EXPECT_FALSE(calledC);

    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

    loop.RunUntilIdle();
    EXPECT_TRUE(calledA);
    EXPECT_FALSE(calledB);
    EXPECT_FALSE(calledC);

    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_1));

    loop.RunUntilIdle();
    EXPECT_TRUE(calledA);
    EXPECT_TRUE(calledB);
    EXPECT_FALSE(calledC);

    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_2));

    loop.RunUntilIdle();
    EXPECT_TRUE(calledA);
    EXPECT_TRUE(calledB);
    EXPECT_TRUE(calledC);

    END_TEST;
}

bool WaitsAreCanceled() {
    BEGIN_TEST;

    async::TestLoop loop;
    zx::event event;
    async::Wait waitA;
    async::Wait waitB;
    async::Wait waitC;
    bool calledA = false;
    bool calledB = false;
    bool calledC = false;

    ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));

    InitWait(&waitA, [&calledA] { calledA = true; }, event, ZX_USER_SIGNAL_0);
    InitWait(&waitB, [&calledB] { calledB = true; }, event, ZX_USER_SIGNAL_0);
    InitWait(&waitC, [&calledC] { calledC = true; }, event, ZX_USER_SIGNAL_0);

    ASSERT_EQ(ZX_OK, waitA.Begin(loop.dispatcher()));
    ASSERT_EQ(ZX_OK, waitB.Begin(loop.dispatcher()));
    ASSERT_EQ(ZX_OK, waitC.Begin(loop.dispatcher()));

    ASSERT_EQ(ZX_OK, waitA.Cancel());
    ASSERT_EQ(ZX_OK, waitC.Cancel());
    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

    loop.RunUntilIdle();
    EXPECT_FALSE(calledA);
    EXPECT_TRUE(calledB);
    EXPECT_FALSE(calledC);

    END_TEST;
}

// Test a task that begins a wait to post a task.
bool NestedTasksAndWaitsAreDispatched() {
    BEGIN_TEST;

    async::TestLoop loop;
    zx::event event;
    async::Wait wait;
    bool wait_begun = false;
    bool wait_dispatched = false;
    bool inner_task_dispatched = false;

    ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));
    InitWait(
        &wait,
        [&] {
            async::PostDelayedTask(loop.dispatcher(),
                                   [&] { inner_task_dispatched = true; },
                                   zx::min(2));
            wait_dispatched = true;
        },
        event,
        ZX_USER_SIGNAL_0);
    async::PostDelayedTask(loop.dispatcher(),
                           [&] {
                               wait.Begin(loop.dispatcher());
                               wait_begun = true;
                           },
                           zx::min(3));

    loop.RunFor(zx::min(3));
    EXPECT_TRUE(wait_begun);
    EXPECT_FALSE(wait_dispatched);
    EXPECT_FALSE(inner_task_dispatched);

    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

    loop.RunUntilIdle();
    EXPECT_TRUE(wait_begun);
    EXPECT_TRUE(wait_dispatched);
    EXPECT_FALSE(inner_task_dispatched);

    loop.RunFor(zx::min(2));
    EXPECT_TRUE(wait_begun);
    EXPECT_TRUE(wait_dispatched);
    EXPECT_TRUE(inner_task_dispatched);

    END_TEST;
}

bool TasksAreDispatchedOnManyLoops() {
    BEGIN_TEST;

    async::TestLoop loop;
    auto loopA = loop.StartNewLoop();
    auto loopB = loop.StartNewLoop();
    auto loopC = loop.StartNewLoop();

    bool called = false;
    bool calledA = false;
    bool calledB = false;
    bool calledC = false;
    async::TaskClosure taskC([&calledC] { calledC = true; });

    async::PostTask(loopB->dispatcher(), [&calledB] { calledB = true; });
    async::PostDelayedTask(loop.dispatcher(), [&called] { called = true; }, zx::sec(1));
    ASSERT_EQ(ZX_OK, taskC.PostDelayed(loopC->dispatcher(), zx::sec(1)));
    async::PostDelayedTask(loopA->dispatcher(), [&calledA] { calledA = true; }, zx::sec(2));

    loop.RunUntilIdle();
    EXPECT_FALSE(called);
    EXPECT_FALSE(calledA);
    EXPECT_TRUE(calledB);
    EXPECT_FALSE(calledC);

    taskC.Cancel();
    loop.RunFor(zx::sec(1));
    EXPECT_TRUE(called);
    EXPECT_FALSE(calledA);
    EXPECT_TRUE(calledB);
    EXPECT_FALSE(calledC);

    loop.RunFor(zx::sec(1));
    EXPECT_TRUE(called);
    EXPECT_TRUE(calledA);
    EXPECT_TRUE(calledB);
    EXPECT_FALSE(calledC);

    END_TEST;
}

bool WaitsAreDispatchedOnManyLoops() {
    BEGIN_TEST;

    async::TestLoop loop;
    auto loopA = loop.StartNewLoop();
    auto loopB = loop.StartNewLoop();
    auto loopC = loop.StartNewLoop();
    async::Wait wait;
    async::Wait waitA;
    async::Wait waitB;
    async::Wait waitC;
    bool called = false;
    bool calledA = false;
    bool calledB = false;
    bool calledC = false;
    zx::event event;

    ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));

    InitWait(&wait, [&called] { called = true; }, event, ZX_USER_SIGNAL_0);
    InitWait(&waitA, [&calledA] { calledA = true; }, event, ZX_USER_SIGNAL_0);
    InitWait(&waitB, [&calledB] { calledB = true; }, event, ZX_USER_SIGNAL_0);
    InitWait(&waitC, [&calledC] { calledC = true; }, event, ZX_USER_SIGNAL_0);

    ASSERT_EQ(ZX_OK, wait.Begin(loop.dispatcher()));
    ASSERT_EQ(ZX_OK, waitA.Begin(loopA->dispatcher()));
    ASSERT_EQ(ZX_OK, waitB.Begin(loopB->dispatcher()));
    ASSERT_EQ(ZX_OK, waitC.Begin(loopC->dispatcher()));

    ASSERT_EQ(ZX_OK, waitB.Cancel());
    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

    loop.RunUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_TRUE(calledA);
    EXPECT_FALSE(calledB);
    EXPECT_TRUE(calledC);

    END_TEST;
}


// Populates |order| with the order in which two tasks and two waits on four
// loops were dispatched, given a |random_seed|.
bool DetermineDispatchOrder(const char* random_seed, int (*order)[4]) {
    BEGIN_HELPER;

    ASSERT_EQ(0, setenv("TEST_LOOP_RANDOM_SEED", random_seed, 1));
    async::TestLoop loop;
    auto loopA = loop.StartNewLoop();
    auto loopB = loop.StartNewLoop();
    auto loopC = loop.StartNewLoop();
    async::Wait wait;
    async::Wait waitB;
    zx::event event;
    int i = 0;

    ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));

    InitWait(&wait, [&] { (*order)[0] = ++i; }, event, ZX_USER_SIGNAL_0);
    async::PostTask(loopA->dispatcher(), [&] { (*order)[1] = ++i; });
    InitWait(&waitB, [&] { (*order)[2] = ++i; }, event, ZX_USER_SIGNAL_0);
    async::PostTask(loopC->dispatcher(), [&] { (*order)[3] = ++i; });

    ASSERT_EQ(ZX_OK, wait.Begin(loop.dispatcher()));
    ASSERT_EQ(ZX_OK, waitB.Begin(loopB->dispatcher()));
    ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

    loop.RunUntilIdle();

    EXPECT_EQ(4, i);
    EXPECT_NE(0, (*order)[0]);
    EXPECT_NE(0, (*order)[1]);
    EXPECT_NE(0, (*order)[2]);
    EXPECT_NE(0, (*order)[3]);

    ASSERT_EQ(0, unsetenv("TEST_LOOP_RANDOM_SEED"));

    END_HELPER;
}

bool DispatchOrderIsDeterministicFor(const char* random_seed) {
    BEGIN_HELPER;

    int expected_order[4] = {0, 0, 0, 0};
    EXPECT_TRUE(DetermineDispatchOrder(random_seed, &expected_order));

    for (int i = 0; i < 5; ++i) {
        int actual_order[4] = {0, 0, 0, 0};
        EXPECT_TRUE(DetermineDispatchOrder(random_seed, &actual_order));
        EXPECT_EQ(expected_order[0], actual_order[0]);
        EXPECT_EQ(expected_order[1], actual_order[1]);
        EXPECT_EQ(expected_order[2], actual_order[2]);
        EXPECT_EQ(expected_order[3], actual_order[3]);
    }

    END_HELPER;
}


bool DispatchOrderIsDeterministic() {
    BEGIN_TEST;

    EXPECT_TRUE(DispatchOrderIsDeterministicFor("1"));
    EXPECT_TRUE(DispatchOrderIsDeterministicFor("43"));
    EXPECT_TRUE(DispatchOrderIsDeterministicFor("893"));
    EXPECT_TRUE(DispatchOrderIsDeterministicFor("39408"));
    EXPECT_TRUE(DispatchOrderIsDeterministicFor("844018"));
    EXPECT_TRUE(DispatchOrderIsDeterministicFor("83018299"));
    EXPECT_TRUE(DispatchOrderIsDeterministicFor("3213"));
    EXPECT_TRUE(DispatchOrderIsDeterministicFor("139133113"));
    EXPECT_TRUE(DispatchOrderIsDeterministicFor("1323234373"));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(SingleLoopTests)
RUN_TEST(DefaultDispatcherIsSetAndUnset)
RUN_TEST(FakeClockTimeIsCorrect)
RUN_TEST(TasksAreDispatched)
RUN_TEST(SameDeadlinesDispatchInPostingOrder)
RUN_TEST(NestedTasksAreDispatched)
RUN_TEST(TimeIsCorrectWhileDispatching)
RUN_TEST(TasksAreCanceled)
RUN_TEST(TimeIsAdvanced)
RUN_TEST(WaitsAreDispatched)
RUN_TEST(NestedWaitsAreDispatched)
RUN_TEST(WaitsAreCanceled)
RUN_TEST(NestedTasksAndWaitsAreDispatched)
END_TEST_CASE(SingleLoopTests)

BEGIN_TEST_CASE(MultiLoopTests)
RUN_TEST(TasksAreDispatchedOnManyLoops)
RUN_TEST(WaitsAreDispatchedOnManyLoops)
RUN_TEST(DispatchOrderIsDeterministic)
END_TEST_CASE(MultiLoopTests)
