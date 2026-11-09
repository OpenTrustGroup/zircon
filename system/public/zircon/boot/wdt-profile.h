// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// The watchdog profile predicate shared by garnet and the boot shim. Both
// read the product devicetree and must agree on whether a watchdog sink is
// configured, so the rule lives here as an inline function. Callers link
// libfdt.

#include <libfdt.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define ZX_WDT_PROFILE_FEATURES_PATH "/features"
#define ZX_WDT_PROFILE_SINK_NAME "wdt-sink"

// Returns true if the node at "offset" has no status property, or a
// well-formed status of "okay" or "ok". Any other value, including a status
// that is not a NUL-terminated string, counts as disabled.
static inline bool zx_wdt_profile_node_okay(const void* dtb, int offset) {
    int len = 0;
    const char* status = (const char*)fdt_getprop(dtb, offset, "status", &len);
    if (status == NULL) {
        return len == -FDT_ERR_NOTFOUND;
    }
    if (len <= 0 || status[len - 1] != '\0' || strlen(status) + 1 != (size_t)len) {
        return false;
    }
    return strcmp(status, "okay") == 0 || strcmp(status, "ok") == 0;
}

// Returns true if the devicetree selects the watchdog sink: both the
// /features node and its wdt-sink child exist and are okay. A NULL or
// malformed devicetree, a missing node, or a disabled, failed, unknown or
// malformed status selects no sink.
static inline bool zx_wdt_sink_enabled(const void* dtb) {
    if (dtb == NULL || fdt_check_header(dtb) != 0) {
        return false;
    }
    int features = fdt_path_offset(dtb, ZX_WDT_PROFILE_FEATURES_PATH);
    if (features < 0 || !zx_wdt_profile_node_okay(dtb, features)) {
        return false;
    }
    int sink = fdt_subnode_offset(dtb, features, ZX_WDT_PROFILE_SINK_NAME);
    if (sink < 0) {
        return false;
    }
    return zx_wdt_profile_node_okay(dtb, sink);
}
