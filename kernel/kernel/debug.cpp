// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @defgroup debug  Debug
 * @{
 */

/**
 * @file
 * @brief  Debug console functions.
 */

#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <lib/console.h>

static int cmd_thread(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadstats(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadload(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadq(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 1
STATIC_COMMAND_MASKED("thread", "manipulate kernel threads", &cmd_thread, CMD_AVAIL_ALWAYS)
#endif
STATIC_COMMAND("threadstats", "thread level statistics", &cmd_threadstats)
STATIC_COMMAND("threadload", "toggle thread load display", &cmd_threadload)
STATIC_COMMAND("threadq", "toggle thread queue display", &cmd_threadq)
STATIC_COMMAND_END(kernel);

#if LK_DEBUGLEVEL > 1
static int cmd_thread(int argc, const cmd_args* argv, uint32_t flags) {

    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("%s bt <thread pointer or id>\n", argv[0].str);
        printf("%s dump <thread pointer or id>\n", argv[0].str);
        printf("%s list\n", argv[0].str);
        printf("%s list_full\n", argv[0].str);
        return -1;
    }

    if (!strcmp(argv[1].str, "bt")) {
        if (argc < 3)
            goto notenoughargs;

        thread_t* t = NULL;
        if (is_kernel_address(argv[2].u)) {
            t = (thread_t *)argv[2].u;
        } else {
            t = thread_id_to_thread_slow(argv[2].u);
        }
        if (t) {
            thread_print_backtrace(t);
        }
    } else if (!strcmp(argv[1].str, "dump")) {
        if (argc < 3)
            goto notenoughargs;

        thread_t* t = NULL;
        if (is_kernel_address(argv[2].u)) {
            t = (thread_t *)argv[2].u;
            dump_thread(t, true);
        } else {
            if (flags & CMD_FLAG_PANIC) {
                dump_thread_user_tid_locked(argv[2].u, true);
            } else {
                dump_thread_user_tid(argv[2].u, true);
            }
        }
    } else if (!strcmp(argv[1].str, "list")) {
        printf("thread list:\n");
        if (flags & CMD_FLAG_PANIC) {
            dump_all_threads_locked(false);
        } else {
            dump_all_threads(false);
        }
    } else if (!strcmp(argv[1].str, "list_full")) {
        printf("thread list:\n");
        if (flags & CMD_FLAG_PANIC) {
            dump_all_threads_locked(true);
        } else {
            dump_all_threads(true);
        }
    } else {
        printf("invalid args\n");
        goto usage;
    }

    // reschedule to let debuglog potentially run
    if (!(flags & CMD_FLAG_PANIC))
        thread_reschedule();

    return 0;
}
#endif

static int cmd_threadstats(int argc, const cmd_args* argv, uint32_t flags) {
    for (uint i = 0; i < SMP_MAX_CPUS; i++) {
        if (!mp_is_cpu_active(i))
            continue;

        printf("thread stats (cpu %u):\n", i);
        printf("\ttotal idle time: %" PRIi64 "\n", percpu[i].stats.idle_time);
        printf("\ttotal busy time: %" PRIi64 "\n",
               zx_time_sub_duration(current_time(), percpu[i].stats.idle_time));
        printf("\treschedules: %lu\n", percpu[i].stats.reschedules);
        printf("\treschedule_ipis: %lu\n", percpu[i].stats.reschedule_ipis);
        printf("\tcontext_switches: %lu\n", percpu[i].stats.context_switches);
        printf("\tpreempts: %lu\n", percpu[i].stats.preempts);
        printf("\tyields: %lu\n", percpu[i].stats.yields);
        printf("\ttimer interrupts: %lu\n", percpu[i].stats.timer_ints);
        printf("\ttimers: %lu\n", percpu[i].stats.timers);
    }

    return 0;
}

namespace {

class RecurringCallback {
public:
    typedef void (*CallbackFunc)();

    RecurringCallback(CallbackFunc callback) : func_(callback) {}

    void Toggle();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(RecurringCallback);

    static void CallbackWrapper(timer_t* t, zx_time_t now, void* arg);

    DECLARE_SPINLOCK(SpinLock) lock_;
    timer_t timer_ = TIMER_INITIAL_VALUE(timer_t);
    bool started_ = false;
    CallbackFunc func_ = nullptr;
};

void RecurringCallback::CallbackWrapper(timer_t* t, zx_time_t now, void *arg) {
    auto cb = static_cast<RecurringCallback*>(arg);
    cb->func_();

    {
        Guard<SpinLock, IrqSave> guard{&cb->lock_};

        if (cb->started_) {
            zx_time_t deadline = zx_time_add_duration(now, ZX_SEC(1));
            timer_set(t, deadline, TIMER_SLACK_CENTER, ZX_MSEC(10), CallbackWrapper, arg);
        }
    }

    // reschedule to give the debuglog a chance to run
    thread_preempt_set_pending();
}

void RecurringCallback::Toggle() {
    Guard<SpinLock, IrqSave> guard{&lock_};

    if (!started_) {
        // start the timer
        timer_set(&timer_, zx_time_add_duration(current_time(), ZX_SEC(1)),
                  TIMER_SLACK_CENTER, ZX_MSEC(10),
                  CallbackWrapper, static_cast<void*>(this));
        started_ = true;
    } else {
        timer_cancel(&timer_);
        started_ = false;
    }
}
}

static int cmd_threadload(int argc, const cmd_args* argv, uint32_t flags) {
    static RecurringCallback cb([]() {
        static struct cpu_stats old_stats[SMP_MAX_CPUS];
        static zx_duration_t last_idle_time[SMP_MAX_CPUS];

        printf("cpu    load"
               " sched (cs ylds pmpts irq_pmpts)"
               "  sysc"
               " ints (hw  tmr tmr_cb)"
               " ipi (rs  gen)\n");
        for (uint i = 0; i < SMP_MAX_CPUS; i++) {
            Guard<spin_lock_t, NoIrqSave> thread_lock_guard{ThreadLock::Get()};

            // dont display time for inactive cpus
            if (!mp_is_cpu_active(i))
                continue;

            zx_duration_t idle_time = percpu[i].stats.idle_time;

            // if the cpu is currently idle, add the time since it went idle up until now to the idle counter
            bool is_idle = !!mp_is_cpu_idle(i);
            if (is_idle) {
                zx_duration_t recent_idle_time =
                    zx_time_sub_time(current_time(), percpu[i].idle_thread.last_started_running);
                idle_time = zx_duration_add_duration(idle_time, recent_idle_time);
            }

            zx_duration_t delta_time = zx_duration_sub_duration(idle_time, last_idle_time[i]);
            zx_duration_t busy_time;
            if (ZX_SEC(1) > delta_time) {
                busy_time = zx_duration_sub_duration(ZX_SEC(1), delta_time);
            } else {
                busy_time = 0;
            }
            zx_duration_t busypercent = zx_duration_mul_int64(busy_time, 10000) / ZX_SEC(1);

            printf("%3u"
                   " %3u.%02u%%"
                   " %9lu %4lu %5lu %9lu"
                   " %5lu"
                   " %8lu %4lu %6lu"
                   " %8lu %4lu"
                   "\n",
                   i,
                   static_cast<uint>(busypercent / 100), static_cast<uint>(busypercent % 100),
                   percpu[i].stats.context_switches - old_stats[i].context_switches,
                   percpu[i].stats.yields - old_stats[i].yields,
                   percpu[i].stats.preempts - old_stats[i].preempts,
                   percpu[i].stats.irq_preempts - old_stats[i].irq_preempts,
                   percpu[i].stats.syscalls - old_stats[i].syscalls,
                   percpu[i].stats.interrupts - old_stats[i].interrupts,
                   percpu[i].stats.timer_ints - old_stats[i].timer_ints,
                   percpu[i].stats.timers - old_stats[i].timers,
                   percpu[i].stats.reschedule_ipis - old_stats[i].reschedule_ipis,
                   percpu[i].stats.generic_ipis - old_stats[i].generic_ipis);

            old_stats[i] = percpu[i].stats;
            last_idle_time[i] = idle_time;
        }
    });

    cb.Toggle();

    return 0;
}

static int cmd_threadq(int argc, const cmd_args* argv, uint32_t flags) {
    static RecurringCallback cb([]() {
        for (uint i = 0; i < SMP_MAX_CPUS; i++) {
            Guard<spin_lock_t, NoIrqSave> thread_lock_guard{ThreadLock::Get()};

            // dont display time for inactive cpus
            if (!mp_is_cpu_active(i))
                continue;

            const struct percpu* cpu = &percpu[i];

            printf("cpu %2u:", i);
            for (uint p = 0; p < NUM_PRIORITIES; p++) {
                printf(" %2zu", list_length(&cpu->run_queue[p]));
            }
            printf("\n");
        }
    });

    cb.Toggle();

    return 0;
}
