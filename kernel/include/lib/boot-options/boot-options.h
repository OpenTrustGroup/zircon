// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_LIB_BOOT_OPTIONS_BOOT_OPTIONS_H_
#define ZIRCON_KERNEL_INCLUDE_LIB_BOOT_OPTIONS_BOOT_OPTIONS_H_

#include <stdint.h>

// A minimal 2018 stand-in for upstream's boot-options library. Only the
// user-pager overtime knobs consumed by PagerProxy::WaitOnEvent are provided;
// gBootOptions is populated from the kernel command line at boot (see
// pager_proxy.cpp). Defaults match upstream: warn after 20s, abort after 300s.
struct BootOptions {
    uint64_t userpager_overtime_wait_seconds = 20;
    uint64_t userpager_overtime_timeout_seconds = 300;
};

extern BootOptions* gBootOptions;

#endif  // ZIRCON_KERNEL_INCLUDE_LIB_BOOT_OPTIONS_BOOT_OPTIONS_H_
