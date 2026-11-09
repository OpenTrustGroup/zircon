// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

zx_status_t _zx_system_get_bootmode(uint64_t* bootmode) {
    *bootmode = DATA_CONSTANTS.bootmode;
    return ZX_OK;
}

VDSO_INTERFACE_FUNCTION(zx_system_get_bootmode);
