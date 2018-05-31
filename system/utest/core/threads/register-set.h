// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/syscalls/debug.h>

// This provides some utilities for testing that sets of register values
// are reported correctly.

#if defined(__x86_64__)
#define REG_PC rip
#define REG_STACK_PTR rsp
#elif defined(__aarch64__)
#define REG_PC pc
#define REG_STACK_PTR sp
#else
# error Unsupported architecture
#endif

// This initializes the register set with arbitrary test data.
void regs_fill_test_values(zx_thread_state_general_regs_t* regs);

// This returns whether the two register sets' values are equal.
bool regs_expect_eq(zx_thread_state_general_regs_t* regs1, zx_thread_state_general_regs_t* regs2);

// The functions below are assembly.
__BEGIN_CDECLS;

// This function sets the registers to the state specified by |regs| and
// then spins, executing a single-instruction infinite loop whose address
// is |spin_address|.
void spin_with_regs(zx_thread_state_general_regs_t* regs);
void spin_with_regs_spin_address(void);

// This assembly code routine saves the registers into a
// zx_thread_state_general_regs_t pointed to by the stack pointer, and then
// calls zx_thread_exit().
void save_regs_and_exit_thread(void);

__END_CDECLS;
