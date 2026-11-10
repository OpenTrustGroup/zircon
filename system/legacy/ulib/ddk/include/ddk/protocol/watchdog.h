// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// The protocol a watchdog driver registers with the platform bus through
// pbus_set_protocol(ZX_PROTOCOL_WDT, ...).
typedef struct wdt_protocol_ops {
    // Enables the watchdog and returns a handle describing the controller.
    zx_status_t (*wdt_enable)(void* ctx, zx_handle_t* handle_out);
} wdt_protocol_ops_t;

typedef struct wdt_protocol {
    wdt_protocol_ops_t* ops;
    void* ctx;
} wdt_protocol_t;

static inline zx_status_t wdt_enable(wdt_protocol_t* wdt, zx_handle_t* handle_out) {
    return wdt->ops->wdt_enable(wdt->ctx, handle_out);
}

__END_CDECLS;
