// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>
#include <lib/zx/vmo.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t vmo::create(uint64_t size, uint32_t options, vmo* result) {
    return zx_vmo_create(size, options, result->reset_and_get_address());
}

zx_status_t vmo::create_ns_mem(const resource& shm_rsc, zx_paddr_t base, size_t size,
                               vmo* result, eventpair* notifier) {
    eventpair event;
    vmo vmo;
    zx_status_t res = zx_vmo_create_ns_mem(shm_rsc.get(), base, size,
                                           vmo.reset_and_get_address(),
                                           event.reset_and_get_address());
    result->reset(vmo.release());
    if (notifier) {
        notifier->reset(event.release());
    }

    return res;
}

} // namespace zx
