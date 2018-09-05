/*
 * Copyright (c) 2018 Open Trust Group
 * Copyright (c) 2013-2016 Google Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <err.h>
#include <trace.h>
#include <kernel/event.h>
#include <kernel/thread.h>
#include <lib/sm.h>
#include <lk/init.h>
#include <string.h>
#include <sys/types.h>

#include <pdev/driver.h>
#include <zircon/boot/driver-config.h>

#define LOCAL_TRACE 0


struct sm_stdcall_state {
    spin_lock_t lock;
    event_t event;
    smc32_args_t args;
    long ret;
    bool done;
    int active_cpu; /* cpu that expects stdcall result */
    int initial_cpu; /* Debug info: cpu that started stdcall */
    int last_cpu; /* Debug info: most recent cpu expecting stdcall result */
    int restart_count;
};

struct sm_percpu {
    event_t nsirqevent;
    thread_t* nsirqthread;
    thread_t* nsidlethread;
    bool irq_thread_ready;
};

struct sm {
    uint32_t api_version;
    bool api_version_locked;
    spin_lock_t api_version_lock;

    struct sm_percpu percpu[SMP_MAX_CPUS];
    thread_t* stdcallthread;
    bool ns_threads_started;
    struct sm_stdcall_state stdcall_state;
    ns_shm_info_t ns_shm;
};

static struct sm sm = {
    .stdcall_state = {
        .event = EVENT_INITIAL_VALUE(sm.stdcall_state.event, 0, 0),
        .active_cpu = -1,
        .initial_cpu = -1,
        .last_cpu = -1,
    },
};

extern smc32_handler_t sm_stdcall_table[];
extern smc32_handler_t sm_nopcall_table[];

static inline struct sm_percpu* sm_get_local_percpu(void) {
    return &sm.percpu[arch_curr_cpu_num()];
}

long smc_sm_api_version(smc32_args_t *args)
{
    uint32_t api_version = args->params[0];

    spin_lock(&sm.api_version_lock);
    if (!sm.api_version_locked) {
        LTRACEF("request api version %u\n", api_version);
        if (api_version > TRUSTY_API_VERSION_CURRENT)
            api_version = TRUSTY_API_VERSION_CURRENT;

        sm.api_version = api_version;
    } else {
        TRACEF("ERROR: Tried to select api version %u after use, current version %u\n",
               api_version, sm.api_version);
        api_version = sm.api_version;
    }
    spin_unlock(&sm.api_version_lock);

    LTRACEF("return api version %u\n", api_version);
    return api_version;
}

static uint32_t sm_get_api_version(void)
{
    if (!sm.api_version_locked) {
        spin_lock_saved_state_t state;
        spin_lock_save(&sm.api_version_lock, &state, SPIN_LOCK_FLAG_IRQ_FIQ);
        sm.api_version_locked = true;
        TRACEF("lock api version %u\n", sm.api_version);
        spin_unlock_restore(&sm.api_version_lock, state, SPIN_LOCK_FLAG_IRQ_FIQ);
    }
    return sm.api_version;
}

void sm_get_shm_config(ns_shm_info_t* shm)
{
    if (shm) {
        memcpy(shm, &sm.ns_shm, sizeof(ns_shm_info_t));
    }
}

static void sm_ns_shm_init(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_sm_ns_shm_t));
    const dcfg_sm_ns_shm_t* ns_shm_cfg = driver_data;

    sm.ns_shm.pa = ns_shm_cfg->base_phys;
    sm.ns_shm.size = ns_shm_cfg->length;
    sm.ns_shm.use_cache = ns_shm_cfg->use_cache;
}

LK_PDEV_INIT(libsm_ns_shm_init, KDRV_SM_NS_SHM, sm_ns_shm_init, LK_INIT_LEVEL_PLATFORM_EARLY);

/* must be called with irqs disabled */
static long sm_queue_stdcall(smc32_args_t *args)
{
    long ret;
    uint cpu_num = arch_curr_cpu_num();
    struct sm_stdcall_state* s = &sm.stdcall_state;

    spin_lock(&s->lock);

    if (s->event.signaled || s->done) {
        if (args->smc_nr == SMC_SC_RESTART_LAST && s->active_cpu == -1) {
            s->restart_count++;
            LTRACEF_LEVEL(3, "cpu %u, restart std call, restart_count %d\n",
                    cpu_num, s->restart_count);
            goto restart_stdcall;
        }
        dprintf(CRITICAL, "%s: cpu %u, std call busy\n", __func__, cpu_num);
        ret = SM_ERR_BUSY;
        goto err;
    } else {
        if (args->smc_nr == SMC_SC_RESTART_LAST) {
            dprintf(CRITICAL, "%s: cpu %u, unexpected restart, no std call active\n",
                __func__, arch_curr_cpu_num());
            ret = SM_ERR_UNEXPECTED_RESTART;
            goto err;
        }
    }

    LTRACEF("cpu %u, queue std call 0x%x\n", cpu_num, args->smc_nr);
    s->initial_cpu = cpu_num;
    s->ret = SM_ERR_INTERNAL_FAILURE;
    s->args = *args;
    s->restart_count = 0;
    event_signal(&s->event, false);

restart_stdcall:
    s->active_cpu = (int)cpu_num;
    ret = 0;

err:
    spin_unlock(&s->lock);

    return ret;
}

/* must be called with irqs disabled */
static void sm_return_and_wait_for_next_stdcall(long ret, uint cpu_num)
{
    smc32_args_t args = SMC32_ARGS_INITIAL_VALUE(args);

    do {
        arch_disable_fiqs();
        LTRACEF("return to NS, on cpu %u\n", arch_curr_cpu_num());
        sm_sched_nonsecure(ret, &args);
        arch_enable_fiqs();

        /* Allow concurrent SMC_SC_NOP calls on multiple cpus */
        if (args.smc_nr == SMC_SC_NOP) {
            LTRACEF_LEVEL(3, "cpu %u, got nop\n", cpu_num);
            ret = sm_nopcall_table[SMC_ENTITY(args.params[0])](&args);
        } else {
            ret = sm_queue_stdcall(&args);
        }
    } while (ret);
}

static void sm_irq_return_ns(void)
{
    long ret;
    uint cpu_num = arch_curr_cpu_num();
    struct sm_stdcall_state* s = &sm.stdcall_state;

    spin_lock(&s->lock); /* TODO: remove? */
    LTRACEF_LEVEL(2, "got irq on cpu %u, stdcallcpu %d\n",
              cpu_num, sm.stdcall_state.active_cpu);

    if (s->active_cpu == (int)cpu_num) {
        s->last_cpu = s->active_cpu;
        s->active_cpu = -1;
        ret = SM_ERR_INTERRUPTED;
    } else {
        ret = SM_ERR_NOP_INTERRUPTED;
    }

    LTRACEF_LEVEL(2, "got irq on cpu %u, return %ld\n", cpu_num, ret);
    spin_unlock(&s->lock);

    sm_return_and_wait_for_next_stdcall(ret, cpu_num);
}

static int __NO_RETURN sm_irq_loop(void *arg)
{
    uint cpu_num;
    uint eventcpu = (uintptr_t)arg; /* cpu that requested this thread, the current cpu could be different */

    /*
     * Run this thread with interrupts masked, so we don't reenter the
     * interrupt handler. The interrupt handler for non-secure interrupts
     * returns to this thread with the interrupt still pending.
     */
    arch_disable_ints();
    sm.percpu[eventcpu].irq_thread_ready = true;

    cpu_num = arch_curr_cpu_num();
    LTRACEF("wait for irqs for cpu %u, on cpu %u\n", eventcpu, cpu_num);
    while (true) {
        event_wait(&sm.percpu[eventcpu].nsirqevent);
        sm_irq_return_ns();
    }
}

/* must be called with irqs disabled */
static long sm_get_stdcall_ret(void)
{
    long ret;
    uint cpu_num = arch_curr_cpu_num();
    struct sm_stdcall_state* s = &sm.stdcall_state;

    spin_lock(&s->lock);

    if (s->active_cpu != (int)cpu_num) {
        dprintf(CRITICAL, "%s: stdcallcpu %d != curr-cpu %u, last %d, initial %d\n",
            __func__, s->active_cpu, cpu_num,
            s->last_cpu, s->initial_cpu);
        ret = SM_ERR_INTERNAL_FAILURE;
        goto err;
    }
    s->last_cpu = s->active_cpu;
    s->active_cpu = -1;

    if (s->done) {
        s->done = false;
        ret = s->ret;
        LTRACEF("cpu %u, return stdcall result, %ld, initial cpu %d\n",
            cpu_num, s->ret, s->initial_cpu);
    } else {
        if (sm_get_api_version() >= TRUSTY_API_VERSION_SMP) /* ns using new api */
            ret = SM_ERR_CPU_IDLE;
        else if (s->restart_count)
            ret = SM_ERR_BUSY;
        else
            ret = SM_ERR_INTERRUPTED;
        LTRACEF("cpu %u, initial cpu %d, restart_count %d, std call not finished, return %ld\n",
            cpu_num, s->initial_cpu, s->restart_count, ret);
    }
err:
    spin_unlock(&s->lock);

    return ret;
}

static int __NO_RETURN sm_wait_for_smcall(void *arg __UNUSED)
{
    uint cpu_num;
    long ret = 0;

    /* We should guarantee all TEE interrupts are handled before return to
     * normal world during boot process, or UEFI bootloader will get panic
     * due to IRQ exception occurred.
     */
    thread_sleep_relative(ZX_MSEC(500));

    LTRACEF("wait for stdcalls, on cpu %u\n", arch_curr_cpu_num());

    while (true) {
        /*
         * Disable interrupts so stdcallstate.active_cpu does not
         * change to or from this cpu after checking it below.
         */
        arch_disable_ints();

        /* Switch to stdcall thread if sm_queue_stdcall woke it up */
        thread_yield();

        cpu_num = arch_curr_cpu_num();
        if (cpu_num == (uint)sm.stdcall_state.active_cpu)
            ret = sm_get_stdcall_ret();
        else
            ret = SM_ERR_NOP_DONE;

        sm_return_and_wait_for_next_stdcall(ret, cpu_num);

        /* Re-enable interrupts (needed for SMC_SC_NOP) */
        arch_enable_ints();
    }
}

static void sm_secondary_init(uint level)
{
    char name[32];
    struct sm_percpu* cpu = sm_get_local_percpu();
    uint cpu_num = arch_curr_cpu_num();

    event_init(&sm.percpu[cpu_num].nsirqevent, false, EVENT_FLAG_AUTOUNSIGNAL);

    snprintf(name, sizeof(name), "irq-ns-switch-%u", cpu_num);
    cpu->nsirqthread = thread_create(name, sm_irq_loop, (void *)(uintptr_t)cpu_num,
                     HIGHEST_PRIORITY, DEFAULT_STACK_SIZE);
    if (!cpu->nsirqthread) {
        panic("failed to create irq NS switcher thread for cpu %u!\n", cpu_num);
    }
    thread_set_cpu_affinity(cpu->nsirqthread, cpu_num_to_mask(cpu_num));
    thread_set_real_time(cpu->nsirqthread);

    snprintf(name, sizeof(name), "idle-ns-switch-%u", cpu_num);
    cpu->nsidlethread = thread_create(name, sm_wait_for_smcall, NULL,
                      LOWEST_PRIORITY + 1, DEFAULT_STACK_SIZE);
    if (!cpu->nsidlethread) {
        panic("failed to create idle NS switcher thread for cpu %u!\n", cpu_num);
    }
    thread_set_cpu_affinity(cpu->nsidlethread, cpu_num_to_mask(cpu_num));
    thread_set_real_time(cpu->nsidlethread);

    if (sm.ns_threads_started) {
        thread_resume(cpu->nsirqthread);
        thread_resume(cpu->nsidlethread);
    }
}

LK_INIT_HOOK_FLAGS(libsm_cpu, sm_secondary_init, LK_INIT_LEVEL_PLATFORM - 2, LK_INIT_FLAG_ALL_CPUS);

static int __NO_RETURN sm_stdcall_loop(void *arg)
{
    long ret;
    spin_lock_saved_state_t state;
    uint cpu_num = arch_curr_cpu_num();
    struct sm_stdcall_state* s = &sm.stdcall_state;

    while (true) {
        LTRACEF("cpu %u, wait for stdcall\n", cpu_num);
        event_wait(&s->event);

        /* Dispatch 'standard call' handler */
        LTRACEF("cpu %u, got stdcall: 0x%x, 0x%x, 0x%x, 0x%x\n",
            cpu_num, s->args.smc_nr,
            s->args.params[0], s->args.params[1], s->args.params[2]);

        ret = sm_stdcall_table[SMC_ENTITY(s->args.smc_nr)](&s->args);

        LTRACEF("cpu %u, stdcall(0x%x, 0x%x, 0x%x, 0x%x) returned 0x%lx (%ld)\n",
            cpu_num, s->args.smc_nr,
            s->args.params[0], s->args.params[1], s->args.params[2], (unsigned long)ret, ret);

        spin_lock_irqsave(&s->lock, state);
        s->ret = ret;
        s->done = true;
        event_unsignal(&s->event);
        spin_unlock_irqrestore(&s->lock, state);
    }
}

static void sm_init(uint level)
{
    sm.stdcallthread = thread_create("sm-stdcall", sm_stdcall_loop, NULL,
                      LOWEST_PRIORITY + 2, DEFAULT_STACK_SIZE);
    if (!sm.stdcallthread) {
        panic("failed to create sm-stdcall thread!\n");
    }
    thread_set_real_time(sm.stdcallthread);
    thread_resume(sm.stdcallthread);
}

LK_INIT_HOOK(libsm, sm_init, LK_INIT_LEVEL_PLATFORM - 1);

void sm_handle_irq(void) {
    struct sm_percpu* cpu = sm_get_local_percpu();

    if (cpu->irq_thread_ready) {
        event_signal(&cpu->nsirqevent, false);
        thread_preempt_set_pending();
    } else {
        TRACEF("warning: got ns irq before irq thread is ready\n");
        sm_irq_return_ns();
    }
}

static void resume_nsthreads(uint level)
{
    int i;

    sm.ns_threads_started = true;
    smp_mb();
    for (i = 0; i < SMP_MAX_CPUS; i++) {
        if (sm.percpu[i].nsirqthread)
            thread_resume(sm.percpu[i].nsirqthread);
        if (sm.percpu[i].nsidlethread)
            thread_resume(sm.percpu[i].nsidlethread);
    }
}

LK_INIT_HOOK(libsm_resume_nsthreads, resume_nsthreads, LK_INIT_LEVEL_LAST);
