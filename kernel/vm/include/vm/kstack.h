// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <err.h>
#include <sys/types.h>

__BEGIN_CDECLS

// kstack encapsulates a kernel stack.
//
// kstack must be a C struct because it is embedded in thread_t.
typedef struct kstack {
    vaddr_t base;
    size_t size;
    vaddr_t top;

    // When non-null, |vmar| (and, if safe-stack is enabled, |unsafe_vmar|) points to a ref-counted
    // VmAddressRegion that must be freed via |vm_free_kstack|.
    //
    // Note, the type is void* rather than |fbl::RefPtr| because this struct is used by C code.
    void* vmar;
#if __has_feature(safe_stack)
    vaddr_t unsafe_base;
    // See comment for |vmar|.
    void* unsafe_vmar;
#endif
} kstack_t;

// Allocates a kernel stack with appropriate overrun padding.
//
// Assumes stack has been zero-initialized.
zx_status_t vm_allocate_kstack(kstack_t* stack);

// Frees a stack allocated by |vm_allocate_kstack|.
zx_status_t vm_free_kstack(kstack_t* stack);

__END_CDECLS
