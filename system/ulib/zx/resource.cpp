// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/resource.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t resource::create(const resource& parent,
                             uint32_t options,
                             uint64_t base,
                             size_t len,
                             const char* name,
                             size_t namelen,
                             resource* result) {
    resource h;
    zx_status_t status = zx_resource_create(parent.get(), options, base, len, name, namelen,
                                            h.reset_and_get_address());
    result->reset(h.release());
    return status;
}

zx_status_t resource::create_ns_mem(uint32_t options,
                                    zx_info_ns_shm_t* shm_info,
                                    resource* result) {
    resource h;
    zx_status_t status = zx_resource_create_ns_mem(options, shm_info, h.reset_and_get_address());
    result->reset(h.release());
    return status;
}

} // namespace zx
