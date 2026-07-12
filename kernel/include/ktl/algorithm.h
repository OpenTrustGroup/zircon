// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_ALGORITHM_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_ALGORITHM_H_

#include <fbl/algorithm.h>

// The 2018 kernel predates the ktl library. Upstream's ktl::min / ktl::max are
// thin aliases for std::min / std::max; map them onto the equivalent fbl helpers
// so ported code that uses them compiles unchanged.
namespace ktl {

using fbl::max;
using fbl::min;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_INCLUDE_KTL_ALGORITHM_H_
