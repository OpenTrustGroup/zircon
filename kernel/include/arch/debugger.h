// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

__BEGIN_CDECLS

struct thread;

// The caller is responsible for making sure the thread is in an exception
// or is suspended, and stays so.
zx_status_t arch_get_general_regs(struct thread* thread, zx_thread_state_general_regs* out);
zx_status_t arch_set_general_regs(struct thread* thread, const zx_thread_state_general_regs* in);

zx_status_t arch_get_fp_regs(struct thread* thread, zx_thread_state_fp_regs* out);
zx_status_t arch_set_fp_regs(struct thread* thread, const zx_thread_state_fp_regs* in);

zx_status_t arch_get_single_step(struct thread* thread, bool* single_step);
zx_status_t arch_set_single_step(struct thread* thread, bool single_step);

zx_status_t arch_get_vector_regs(struct thread* thread, zx_thread_state_vector_regs* out);
zx_status_t arch_set_vector_regs(struct thread* thread, const zx_thread_state_vector_regs* in);

// Only relevant on x86. Returns ZX_ERR_NOT_SUPPORTED on ARM.
zx_status_t arch_get_x86_register_fs(struct thread* thread, uint64_t* out);
zx_status_t arch_set_x86_register_fs(struct thread* thread, const uint64_t* in);

// Only relevant on x86. Returns ZX_ERR_NOT_SUPPORTED on ARM.
zx_status_t arch_get_x86_register_gs(struct thread* thread, uint64_t* out);
zx_status_t arch_set_x86_register_gs(struct thread* thread, const uint64_t* in);

__END_CDECLS
