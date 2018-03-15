// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <stdlib.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Wait for a "parent" device to have a child "driver" bound.
//
// Return 0 on success, -1 on error.
int wait_for_driver_bind(const char* parent, const char* driver);

// Creates a ramdisk  returns the full path to the ramdisk in ramdisk_path_out.
// This path should be at least PATH_MAX characters long.
//
// Return 0 on success, -1 on error.
int create_ramdisk(uint64_t blk_size, uint64_t blk_count, char* out_path);

// Same but uses an existing VMO as the ramdisk.
// The handle is always consumed, and must be the only handle to this VMO.
int create_ramdisk_from_vmo(zx_handle_t vmo, char* out_path);

// Puts the ramdisk at |ramdisk_path| to sleep after |txn_count| transactions
// After this, transactions will no longer be successfully persisted to disk.
int sleep_ramdisk(const char* ramdisk_path, uint64_t txn_count);

// Wake the ramdisk at |ramdisk_path| from a sleep state.
int wake_ramdisk(const char* ramdisk_path);

// Return the number of transactions completed by ramdisk at |ramdisk_path| since
// the ramdisk has been awake.
int get_ramdisk_txns(const char* ramdisk_path, uint64_t* txn_count);

// Destroys a ramdisk, given the "ramdisk_path" returned from "create_ramdisk".
//
// Return 0 on success, -1 on error.
int destroy_ramdisk(const char* ramdisk_path);

__END_CDECLS
