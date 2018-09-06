// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxcpp/new.h>

#include <zircon/assert.h>
#include <stdlib.h>

#if !_KERNEL
// The kernel does not want non-AllocCheckered non-placement new
// overloads, but userspace can have them.
void* operator new(size_t s) {
    if (s == 0u) {
        s = 1u;
    }
    auto mem = ::malloc(s);
    if (!mem) {
        ZX_PANIC("Out of memory (new)\n");
    }
    return mem;
}

void* operator new[](size_t s) {
    if (s == 0u) {
        s = 1u;
    }
    auto mem = ::malloc(s);
    if (!mem) {
        ZX_PANIC("Out of memory (new[])\n");
    }
    return mem;
}

void* operator new(size_t s, const std::nothrow_t&) noexcept {
    if (s == 0u) {
        s = 1u;
    }
    return ::malloc(s);
}

void* operator new[](size_t s, const std::nothrow_t&) noexcept {
    if (s == 0u) {
        s = 1u;
    }
    return ::malloc(s);
}

#else // _KERNEL

// kernel versions may pass through the call site to the underlying allocator
void* operator new(size_t s, const std::nothrow_t&) noexcept {
    if (s == 0u) {
        s = 1u;
    }
    return ::malloc_debug_caller(s, __GET_CALLER());
}

void* operator new[](size_t s, const std::nothrow_t&) noexcept {
    if (s == 0u) {
        s = 1u;
    }
    return ::malloc_debug_caller(s, __GET_CALLER());
}

#endif // _KERNEL

void operator delete(void *p) {
    return ::free(p);
}

void operator delete[](void *p) {
    return ::free(p);
}

void operator delete(void *p, size_t s) {
    return ::free(p);
}

void operator delete[](void *p, size_t s) {
    return ::free(p);
}
void* operator new(size_t , void *p) {
    return p;
}

void* operator new[](size_t , void* p) {
    return p;
}

