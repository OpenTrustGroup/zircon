// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Boot modes zx_system_get_bootmode reports.
//
// ZX_BOOTMODE_PHYP: this kernel runs as an ordinary OS.
// ZX_BOOTMODE_AHYP: this kernel is the hypervisor that runs the guests: it
//                   started at EL2.
// ZX_BOOTMODE_TEE:  this kernel is a TEE. NORA has none; the value is only
//                   reserved.
#define ZX_BOOTMODE_PHYP 0u
#define ZX_BOOTMODE_AHYP 1u
#define ZX_BOOTMODE_TEE  2u
