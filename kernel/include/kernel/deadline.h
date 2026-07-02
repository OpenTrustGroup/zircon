// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_DEADLINE_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_DEADLINE_H_

#include <platform.h>
#include <zircon/types.h>

// A minimal stand-in for upstream's Deadline (no TimerSlack). It represents an
// absolute monotonic deadline and converts to the zx_time_t that the 2018
// Event::Wait() expects, so upstream code using Deadline compiles unchanged.
class Deadline {
public:
    static Deadline infinite() { return Deadline(ZX_TIME_INFINITE); }
    static Deadline after(zx_duration_t after) { return Deadline(current_time() + after); }

    zx_time_t when() const { return when_; }
    operator zx_time_t() const { return when_; }

private:
    explicit Deadline(zx_time_t when) : when_(when) {}
    zx_time_t when_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_DEADLINE_H_
