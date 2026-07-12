// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_ATOMIC_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_ATOMIC_H_

#include <fbl/atomic.h>

// The 2018 kernel predates the ktl library. Upstream code uses ktl::atomic (a
// thin alias for std::atomic) along with the ktl::memory_order_* enumerators.
// The 2018 tree has no std::atomic in the kernel, but it does have fbl::atomic,
// which offers the same integral/enum atomic surface (load/store/exchange/
// fetch_*/compare_exchange_*). Map ktl::atomic onto fbl::atomic and re-export the
// memory order enumerators so ported code compiles unchanged.
//
// Note: fbl::atomic's compare_exchange_weak takes the expected value by pointer
// (T*) and requires both success and failure memory orders explicitly, whereas
// std::atomic takes a reference and defaults the failure order. Ported call
// sites are adjusted mechanically to match fbl::atomic's spelling where needed.
namespace ktl {

using fbl::atomic;

using fbl::memory_order;
using fbl::memory_order_relaxed;
using fbl::memory_order_acquire;
using fbl::memory_order_release;
using fbl::memory_order_acq_rel;
using fbl::memory_order_seq_cst;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_INCLUDE_KTL_ATOMIC_H_
