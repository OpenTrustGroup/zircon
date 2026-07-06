// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <zircon/syscalls.h>
#include <unittest/unittest.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#if defined(__x86_64__)
#include <cpuid.h>

struct desc_ptr {
    unsigned short size;
    unsigned long address;
} __attribute__((packed)) ;

// If UMIP (User-Mode Instruction Prevention) is supported, the kernel enables
// it. That makes SIDT (like SGDT/SLDT/STR/SMSW) fault with a #GP when executed
// in user mode, so userspace cannot read the IDT address at all -- a strictly
// stronger guarantee than "the IDT is not in the kernel module". See the
// system/utest/x86-umip test for the UMIP behavior itself.
static bool is_umip_supported(void) {
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(7, &eax, &ebx, &ecx, &edx) != 1) {
        return false;
    }
    return ecx & (1u << 2);
}
#endif

bool processor_test(void) {
    BEGIN_TEST;

#if defined(__x86_64__)
    // Check the IDT is not in the kernel module
    // TODO(thgarnie) check all CPUs when sched_setaffinity is implemented
    if (is_umip_supported()) {
        // Executing SIDT here would #GP under UMIP. Userspace being unable to
        // read the IDT address already satisfies what this test checks for, so
        // skip the probe instead of crashing on it.
        unittest_printf("UMIP enabled; SIDT is blocked in user mode, skipping IDT probe\n");
    } else {
        struct desc_ptr idt;
        __asm__ ("sidt %0" : "=m" (idt));
        unittest_printf("IDT address = %lx\n", idt.address);
        EXPECT_LT(idt.address, 0xffffffff80000000UL,
                  "Check IDT is not in the kernel module (remapped)");
    }
#endif

    END_TEST;
}

BEGIN_TEST_CASE(processor_tests)
RUN_TEST(processor_test)
END_TEST_CASE(processor_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
