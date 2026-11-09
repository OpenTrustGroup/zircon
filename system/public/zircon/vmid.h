// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

// VM IDs of the Nebula guests. A user VM ID is the one the guest manager and
// the VMM use (the SOS is -1); a guest VM ID is the hypervisor's own, the
// user VM ID plus ZX_VMID_USER_TO_GUEST_OFFSET.
#define ZX_USER_VMID_SOS (-1)
#define ZX_USER_VMID_ALPS 0
#define ZX_USER_VMID_TBOX 1

#define ZX_VMID_USER_TO_GUEST_OFFSET 2
#define ZX_VMID_USER_TO_GUEST(vmid) ((vmid) + ZX_VMID_USER_TO_GUEST_OFFSET)

#define ZX_GUEST_VMID_SOS ZX_VMID_USER_TO_GUEST(ZX_USER_VMID_SOS)
#define ZX_GUEST_VMID_ALPS ZX_VMID_USER_TO_GUEST(ZX_USER_VMID_ALPS)
#define ZX_GUEST_VMID_TBOX ZX_VMID_USER_TO_GUEST(ZX_USER_VMID_TBOX)

// The guest VM ID of user VM ID |vmid|, as logs show it.
static inline int32_t zx_log_guest_vmid(int32_t vmid) {
    return ZX_VMID_USER_TO_GUEST(vmid);
}
