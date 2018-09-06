// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arch_ops.h>
#include <arch/arm64.h>
#include <arch/arm64/exceptions.h>
#include <arch/exception.h>
#include <arch/user_copy.h>

#include <bits.h>
#include <debug.h>
#include <inttypes.h>

#include <kernel/interrupt.h>
#include <kernel/thread.h>

#include <platform.h>
#include <stdio.h>
#include <trace.h>
#include <vm/fault.h>
#include <vm/vm.h>

#include <lib/counters.h>
#include <lib/crashlog.h>

#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

#define DFSC_ALIGNMENT_FAULT 0b100001

static void dump_iframe(const struct arm64_iframe_long* iframe) {
    printf("iframe %p:\n", iframe);
    printf("x0  %#18" PRIx64 " x1  %#18" PRIx64 " x2  %#18" PRIx64 " x3  %#18" PRIx64 "\n", iframe->r[0], iframe->r[1], iframe->r[2], iframe->r[3]);
    printf("x4  %#18" PRIx64 " x5  %#18" PRIx64 " x6  %#18" PRIx64 " x7  %#18" PRIx64 "\n", iframe->r[4], iframe->r[5], iframe->r[6], iframe->r[7]);
    printf("x8  %#18" PRIx64 " x9  %#18" PRIx64 " x10 %#18" PRIx64 " x11 %#18" PRIx64 "\n", iframe->r[8], iframe->r[9], iframe->r[10], iframe->r[11]);
    printf("x12 %#18" PRIx64 " x13 %#18" PRIx64 " x14 %#18" PRIx64 " x15 %#18" PRIx64 "\n", iframe->r[12], iframe->r[13], iframe->r[14], iframe->r[15]);
    printf("x16 %#18" PRIx64 " x17 %#18" PRIx64 " x18 %#18" PRIx64 " x19 %#18" PRIx64 "\n", iframe->r[16], iframe->r[17], iframe->r[18], iframe->r[19]);
    printf("x20 %#18" PRIx64 " x21 %#18" PRIx64 " x22 %#18" PRIx64 " x23 %#18" PRIx64 "\n", iframe->r[20], iframe->r[21], iframe->r[22], iframe->r[23]);
    printf("x24 %#18" PRIx64 " x25 %#18" PRIx64 " x26 %#18" PRIx64 " x27 %#18" PRIx64 "\n", iframe->r[24], iframe->r[25], iframe->r[26], iframe->r[27]);
    printf("x28 %#18" PRIx64 " x29 %#18" PRIx64 " lr  %#18" PRIx64 " usp %#18" PRIx64 "\n", iframe->r[28], iframe->r[29], iframe->lr, iframe->usp);
    printf("elr  %#18" PRIx64 "\n", iframe->elr);
    printf("spsr %#18" PRIx64 "\n", iframe->spsr);
}

KCOUNTER(exceptions_brkpt, "kernel.exceptions.breakpoint");
KCOUNTER(exceptions_fpu, "kernel.exceptions.fpu");
KCOUNTER(exceptions_page, "kernel.exceptions.page_fault");
KCOUNTER(exceptions_irq, "kernel.exceptions.irq");
KCOUNTER(exceptions_unhandled, "kernel.exceptions.unhandled");
KCOUNTER(exceptions_user, "kernel.exceptions.user");
KCOUNTER(exceptions_unknown, "kernel.exceptions.unknown");

static zx_status_t try_dispatch_user_data_fault_exception(
    zx_excp_type_t type, struct arm64_iframe_long* iframe,
    uint32_t esr, uint64_t far) {
    thread_t* thread = get_current_thread();
    arch_exception_context_t context = {};
    DEBUG_ASSERT(iframe != nullptr);
    context.frame = iframe;
    context.esr = esr;
    context.far = far;

    arch_enable_ints();
    DEBUG_ASSERT(thread->arch.suspended_general_regs == nullptr);
    thread->arch.suspended_general_regs = iframe;
    zx_status_t status = dispatch_user_exception(type, &context);
    thread->arch.suspended_general_regs = nullptr;
    arch_disable_ints();
    return status;
}

static zx_status_t try_dispatch_user_exception(
    zx_excp_type_t type, struct arm64_iframe_long* iframe, uint32_t esr) {
    return try_dispatch_user_data_fault_exception(type, iframe, esr, 0);
}

__NO_RETURN static void exception_die(struct arm64_iframe_long* iframe, uint32_t esr) {
    platform_panic_start();

    uint32_t ec = BITS_SHIFT(esr, 31, 26);
    uint32_t il = BIT(esr, 25);
    uint32_t iss = BITS(esr, 24, 0);

    /* fatal exception, die here */
    printf("ESR 0x%x: ec 0x%x, il 0x%x, iss 0x%x\n", esr, ec, il, iss);
    dump_iframe(iframe);
    crashlog.iframe = iframe;

    platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_PANIC);
}

static void arm64_unknown_handler(struct arm64_iframe_long* iframe, uint exception_flags,
                                  uint32_t esr) {
    /* this is for a lot of reasons, but most of them are undefined instructions */
    if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
        /* trapped inside the kernel, this is bad */
        printf("unknown exception in kernel: PC at %#" PRIx64 "\n", iframe->elr);
        exception_die(iframe, esr);
    }
    try_dispatch_user_exception(ZX_EXCP_UNDEFINED_INSTRUCTION, iframe, esr);
}

static void arm64_brk_handler(struct arm64_iframe_long* iframe, uint exception_flags,
                              uint32_t esr) {
    if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
        /* trapped inside the kernel, this is bad */
        printf("BRK in kernel: PC at %#" PRIx64 "\n", iframe->elr);
        exception_die(iframe, esr);
    }
    try_dispatch_user_exception(ZX_EXCP_SW_BREAKPOINT, iframe, esr);
}

static void arm64_step_handler(struct arm64_iframe_long* iframe, uint exception_flags,
                               uint32_t esr) {
    if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
        /* trapped inside the kernel, this is bad */
        printf("software step in kernel: PC at %#" PRIx64 "\n", iframe->elr);
        exception_die(iframe, esr);
    }
    try_dispatch_user_exception(ZX_EXCP_HW_BREAKPOINT, iframe, esr);
}

static void arm64_fpu_handler(struct arm64_iframe_long* iframe, uint exception_flags,
                              uint32_t esr) {
    if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
        /* we trapped a floating point instruction inside our own EL, this is bad */
        printf("invalid fpu use in kernel: PC at %#" PRIx64 "\n",
               iframe->elr);
        exception_die(iframe, esr);
    }
    arm64_fpu_exception(iframe, exception_flags);
}

static void arm64_instruction_abort_handler(struct arm64_iframe_long* iframe, uint exception_flags,
                                            uint32_t esr) {
    /* read the FAR register */
    uint64_t far = ARM64_READ_SYSREG(far_el1);
    uint32_t ec = BITS_SHIFT(esr, 31, 26);
    uint32_t iss = BITS(esr, 24, 0);
    bool is_user = !BIT(ec, 0);

    uint pf_flags = VMM_PF_FLAG_INSTRUCTION;
    pf_flags |= is_user ? VMM_PF_FLAG_USER : 0;
    /* Check if this was not permission fault */
    if ((iss & 0b111100) != 0b001100) {
        pf_flags |= VMM_PF_FLAG_NOT_PRESENT;
    }

    LTRACEF("instruction abort: PC at %#" PRIx64
            ", is_user %d, FAR %" PRIx64 ", esr 0x%x, iss 0x%x\n",
            iframe->elr, is_user, far, esr, iss);

    arch_enable_ints();
    kcounter_add(exceptions_page, 1);
    CPU_STATS_INC(page_faults);
    zx_status_t err = vmm_page_fault_handler(far, pf_flags);
    arch_disable_ints();
    if (err >= 0)
        return;

    // If this is from user space, let the user exception handler
    // get a shot at it.
    if (is_user) {
        kcounter_add(exceptions_user, 1);
        if (try_dispatch_user_data_fault_exception(ZX_EXCP_FATAL_PAGE_FAULT, iframe, esr, far) == ZX_OK)
            return;
    }

    printf("instruction abort: PC at %#" PRIx64 ", is_user %d, FAR %" PRIx64 "\n",
           iframe->elr, is_user, far);
    exception_die(iframe, esr);
}

static void arm64_data_abort_handler(struct arm64_iframe_long* iframe, uint exception_flags,
                                     uint32_t esr) {
    /* read the FAR register */
    uint64_t far = ARM64_READ_SYSREG(far_el1);
    uint32_t ec = BITS_SHIFT(esr, 31, 26);
    uint32_t iss = BITS(esr, 24, 0);
    bool is_user = !BIT(ec, 0);
    bool WnR = BIT(iss, 6); // Write not Read
    bool CM = BIT(iss, 8);  // cache maintenance op

    uint pf_flags = 0;
    // if it was marked Write but the cache maintenance bit was set, treat it as read
    pf_flags |= (WnR && !CM) ? VMM_PF_FLAG_WRITE : 0;
    pf_flags |= is_user ? VMM_PF_FLAG_USER : 0;
    /* Check if this was not permission fault */
    if ((iss & 0b111100) != 0b001100) {
        pf_flags |= VMM_PF_FLAG_NOT_PRESENT;
    }

    LTRACEF("data fault: PC at %#" PRIx64
            ", is_user %d, FAR %#" PRIx64 ", esr 0x%x, iss 0x%x\n",
            iframe->elr, is_user, far, esr, iss);

    uint32_t dfsc = BITS(iss, 5, 0);
    if (likely(dfsc != DFSC_ALIGNMENT_FAULT)) {
        arch_enable_ints();
        kcounter_add(exceptions_page, 1);
        zx_status_t err = vmm_page_fault_handler(far, pf_flags);
        arch_disable_ints();
        if (err >= 0) {
            return;
        }
    }

    // Check if the current thread was expecting a data fault and
    // we should return to its handler.
    thread_t* thr = get_current_thread();
    if (thr->arch.data_fault_resume != NULL && is_user_address(far)) {
        iframe->elr = (uintptr_t)thr->arch.data_fault_resume;
        return;
    }

    // If this is from user space, let the user exception handler
    // get a shot at it.
    if (is_user) {
        kcounter_add(exceptions_user, 1);
        zx_excp_type_t excp_type = ZX_EXCP_FATAL_PAGE_FAULT;
        if (unlikely(dfsc == DFSC_ALIGNMENT_FAULT)) {
            excp_type = ZX_EXCP_UNALIGNED_ACCESS;
        }
        if (try_dispatch_user_data_fault_exception(excp_type, iframe, esr, far) == ZX_OK)
            return;
    }

    /* decode the iss */
    if (BIT(iss, 24)) { /* ISV bit */
        printf("data fault: PC at %#" PRIx64
               ", FAR %#" PRIx64 ", iss %#x (DFSC %#x)\n",
               iframe->elr, far, iss, BITS(iss, 5, 0));
    } else {
        printf("data fault: PC at %#" PRIx64
               ", FAR %#" PRIx64 ", iss 0x%x\n",
               iframe->elr, far, iss);
    }

    exception_die(iframe, esr);
}

static inline void arm64_restore_percpu_pointer() {
    arm64_write_percpu_ptr(get_current_thread()->arch.current_percpu_ptr);
}

/* called from assembly */
extern "C" void arm64_sync_exception(
    struct arm64_iframe_long* iframe, uint exception_flags, uint32_t esr) {
    uint32_t ec = BITS_SHIFT(esr, 31, 26);

    if (exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) {
        // if we came from a lower level, restore the per cpu pointer
        arm64_restore_percpu_pointer();
    }

    switch (ec) {
    case 0b000000: /* unknown reason */
        kcounter_add(exceptions_unknown, 1);
        arm64_unknown_handler(iframe, exception_flags, esr);
        break;
    case 0b111000: /* BRK from arm32 */
    case 0b111100: /* BRK from arm64 */
        kcounter_add(exceptions_brkpt, 1);
        arm64_brk_handler(iframe, exception_flags, esr);
        break;
    case 0b000111: /* floating point */
        kcounter_add(exceptions_fpu, 1);
        arm64_fpu_handler(iframe, exception_flags, esr);
        break;
    case 0b010001: /* syscall from arm32 */
    case 0b010101: /* syscall from arm64 */
        printf("syscalls should be handled in assembly\n");
        exception_die(iframe, esr);
        break;
    case 0b100000: /* instruction abort from lower level */
    case 0b100001: /* instruction abort from same level */
        arm64_instruction_abort_handler(iframe, exception_flags, esr);
        break;
    case 0b100100: /* data abort from lower level */
    case 0b100101: /* data abort from same level */
        arm64_data_abort_handler(iframe, exception_flags, esr);
        break;
    case 0b110010: /* software step from lower level */
    case 0b110011: /* software step from same level */
        arm64_step_handler(iframe, exception_flags, esr);
        break;
    default: {
        /* TODO: properly decode more of these */
        if (unlikely((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0)) {
            /* trapped inside the kernel, this is bad */
            printf("unhandled exception in kernel: PC at %#" PRIx64 "\n", iframe->elr);
            exception_die(iframe, esr);
        }
        /* let the user exception handler get a shot at it */
        kcounter_add(exceptions_unhandled, 1);
        if (try_dispatch_user_exception(ZX_EXCP_GENERAL, iframe, esr) == ZX_OK)
            break;
        printf("unhandled synchronous exception\n");
        exception_die(iframe, esr);
    }
    }

    /* if we came from user space, check to see if we have any signals to handle */
    if (unlikely(exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL)) {
        /* in the case of receiving a kill signal, this function may not return,
         * but the scheduler would have been invoked so it's fine.
         */
        arm64_thread_process_pending_signals(iframe);
    }

    /* if we're returning to kernel space, make sure we restore the correct x18 */
    if ((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0) {
        iframe->r[18] = (uint64_t)arm64_read_percpu_ptr();
    }
}

/* called from assembly */
extern "C" uint32_t arm64_irq(struct arm64_iframe_short* iframe, uint exception_flags) {
    if (exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) {
        // if we came from a lower level, restore the per cpu pointer
        arm64_restore_percpu_pointer();
    }

    LTRACEF("iframe %p, flags 0x%x\n", iframe, exception_flags);

    int_handler_saved_state_t state;
    int_handler_start(&state);

    kcounter_add(exceptions_irq, 1);
    platform_irq(iframe);

    bool do_preempt = int_handler_finish(&state);

    /* if we came from user space, check to see if we have any signals to handle */
    if (unlikely(exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL)) {
        uint32_t exit_flags = 0;
        if (thread_is_signaled(get_current_thread()))
            exit_flags |= ARM64_IRQ_EXIT_THREAD_SIGNALED;
        if (do_preempt)
            exit_flags |= ARM64_IRQ_EXIT_RESCHEDULE;
        return exit_flags;
    }

    /* preempt the thread if the interrupt has signaled it */
    if (do_preempt)
        thread_preempt();

    /* if we're returning to kernel space, make sure we restore the correct x18 */
    if ((exception_flags & ARM64_EXCEPTION_FLAG_LOWER_EL) == 0) {
        iframe->r[18] = (uint64_t)arm64_read_percpu_ptr();
    }

    return 0;
}

/* called from assembly */
extern "C" void arm64_finish_user_irq(uint32_t exit_flags, struct arm64_iframe_long* iframe) {
    // we came from a lower level, so restore the per cpu pointer
    arm64_restore_percpu_pointer();

    /* in the case of receiving a kill signal, this function may not return,
     * but the scheduler would have been invoked so it's fine.
     */
    if (unlikely(exit_flags & ARM64_IRQ_EXIT_THREAD_SIGNALED)) {
        DEBUG_ASSERT(iframe != nullptr);
        arm64_thread_process_pending_signals(iframe);
    }

    /* preempt the thread if the interrupt has signaled it */
    if (exit_flags & ARM64_IRQ_EXIT_RESCHEDULE)
        thread_preempt();
}

/* called from assembly */
extern "C" void arm64_invalid_exception(struct arm64_iframe_long* iframe, unsigned int which) {
    // restore the percpu pointer (x18) unconditionally
    arm64_restore_percpu_pointer();

    printf("invalid exception, which 0x%x\n", which);
    dump_iframe(iframe);

    platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_PANIC);
}

/* called from assembly */
extern "C" void arm64_thread_process_pending_signals(struct arm64_iframe_long* iframe) {
    thread_t* thread = get_current_thread();
    DEBUG_ASSERT(iframe != nullptr);
    DEBUG_ASSERT(thread->arch.suspended_general_regs == nullptr);

    thread->arch.suspended_general_regs = iframe;
    thread_process_pending_signals();
    thread->arch.suspended_general_regs = nullptr;
}

void arch_dump_exception_context(const arch_exception_context_t* context) {
    uint32_t ec = BITS_SHIFT(context->esr, 31, 26);
    uint32_t iss = BITS(context->esr, 24, 0);

    switch (ec) {
    case 0b100000: /* instruction abort from lower level */
    case 0b100001: /* instruction abort from same level */
        printf("instruction abort: PC at %#" PRIx64
               ", address %#" PRIx64 " IFSC %#x %s\n",
               context->frame->elr, context->far,
               BITS(context->esr, 5, 0),
               BIT(ec, 0) ? "" : "user ");

        break;
    case 0b100100: /* data abort from lower level */
    case 0b100101: /* data abort from same level */
        printf("data abort: PC at %#" PRIx64
               ", address %#" PRIx64 " %s%s\n",
               context->frame->elr, context->far,
               BIT(ec, 0) ? "" : "user ",
               BIT(iss, 6) ? "write" : "read");
    }

    dump_iframe(context->frame);

    // try to dump the user stack
    if (is_user_address(context->frame->usp)) {
        uint8_t buf[256];
        if (arch_copy_from_user(buf, (void*)context->frame->usp, sizeof(buf)) == ZX_OK) {
            printf("bottom of user stack at 0x%lx:\n", (vaddr_t)context->frame->usp);
            hexdump_ex(buf, sizeof(buf), context->frame->usp);
        }
    }
}

void arch_fill_in_exception_context(const arch_exception_context_t* arch_context, zx_exception_report_t* report) {
    zx_exception_context_t* zx_context = &report->context;

    zx_context->arch.u.arm_64.esr = arch_context->esr;

    // If there was a fatal page fault, fill in the address that caused the fault.
    if (ZX_EXCP_FATAL_PAGE_FAULT == report->header.type) {
        zx_context->arch.u.arm_64.far = arch_context->far;
    } else {
        zx_context->arch.u.arm_64.far = 0;
    }
}

zx_status_t arch_dispatch_user_policy_exception(void) {
    struct arm64_iframe_long frame = {};
    arch_exception_context_t context = {};
    context.frame = &frame;
    return dispatch_user_exception(ZX_EXCP_POLICY_ERROR, &context);
}
