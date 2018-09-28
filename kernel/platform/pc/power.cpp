// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//

#include <arch/mp.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/mp.h>
#include <fbl/atomic.h>
#include <stdio.h>
#include <string.h>

#include <platform.h>
#include <platform/keyboard.h>

#include <lib/console.h>
#include <lib/debuglog.h>
#include <lib/version.h>

// The I/O port to write to for QEMU debug exit.
const uint16_t kQEMUDebugExitPort = 0xf4;

// The return code that we should propagate to QEMU on isa-debug-exit.
// This number must be non-zero and odd, since QEMU calculates the return
// code as (val << 1) | 1 where "val" is the value written to 0xf4.
const uint8_t kQEMUExitCode = 0x1f;
static_assert(kQEMUExitCode != 0 && kQEMUExitCode % 2 != 0,
              "QEMU exit code must be non-zero and odd.");

static void reboot(void) {
    x86_get_microarch_config()->reboot_system();
    // We fell through. Try rebooting via keyboard controller.
    pc_keyboard_reboot();
}

static fbl::atomic<cpu_mask_t> halted_cpus(0);

static void halt_other_cpus(void) {
    static fbl::atomic<int> halted(0);

    if (halted.exchange(1) == 0) {
        // stop the other cpus
        printf("stopping other cpus\n");
        arch_mp_send_ipi(MP_IPI_TARGET_ALL_BUT_LOCAL, 0, MP_IPI_HALT);

        cpu_mask_t targets = mp_get_online_mask() & ~cpu_num_to_mask(arch_curr_cpu_num());
        // spin for a while
        // TODO: find a better way to spin at this low level
        for (volatile int i = 0; i < 100000000; i++) {
            if (halted_cpus.load() == targets) {
                break;
            }
            __asm volatile("nop");
        }

        // Don't send an INIT IPI to the BSP, since that may cause the system to
        // reboot
        x86_force_halt_all_but_local_and_bsp();
    }
}

void platform_halt_cpu(void) {
    // Signal that this CPU is in its halt loop
    halted_cpus.fetch_or(cpu_num_to_mask(arch_curr_cpu_num()));
}

void platform_panic_start(void) {
    platform_debug_panic_start();
    arch_disable_ints();

    static fbl::atomic<int> panic_started(0);
    if (panic_started.exchange(1) == 0) {
        dlog_bluescreen_init();
    }

    halt_other_cpus();
}

bool halt_on_panic = false;
extern const char* manufacturer;

void platform_halt(
    platform_halt_action suggested_action,
    platform_halt_reason reason) {
    printf("platform_halt suggested_action %d reason %d\n", suggested_action, reason);

    arch_disable_ints();

    switch (suggested_action) {
    case HALT_ACTION_SHUTDOWN:
        if (strcmp("QEMU", manufacturer) == 0) {
            outp(kQEMUDebugExitPort, (kQEMUExitCode >> 1));
        }
        printf("Power off failed, halting\n");
        break;
    case HALT_ACTION_REBOOT:
        printf("Rebooting...\n");
        reboot();
        printf("Reboot failed, halting\n");
        break;
    case HALT_ACTION_HALT:
        printf("Halting...\n");
        halt_other_cpus();
        break;
    case HALT_ACTION_REBOOT_BOOTLOADER:
    case HALT_ACTION_REBOOT_RECOVERY:
        printf("platform_halt: Unsupported halt reason %d\n", suggested_action);
        break;
    }

    if (reason == HALT_REASON_SW_PANIC) {
        thread_print_current_backtrace();
        dlog_bluescreen_halt();
    }

    if (!halt_on_panic) {
        printf("Rebooting...\n");
        reboot();
    }

    printf("Halted\n");

#if ENABLE_PANIC_SHELL
    panic_shell_start();
#endif

    for (;;) {
        x86_hlt();
    }
}
