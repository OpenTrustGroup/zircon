// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_MOVE_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_MOVE_H_

#include <fbl/type_support.h>

// The 2018 kernel predates the ktl library. Upstream's ktl::move / ktl::forward
// are thin aliases for std::move / std::forward; map them onto the equivalent
// fbl helpers so upstream code that uses them compiles unchanged.
namespace ktl {

using fbl::forward;
using fbl::move;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_INCLUDE_KTL_MOVE_H_
