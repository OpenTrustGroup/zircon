// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <digest/digest.h>
#include <gpt/gpt.h>
#include <zircon/device/block.h>
#include <stdlib.h>
#include <string.h>

#define FVM_MAGIC (0x54524150204d5646ull) // 'FVM PART'
#define FVM_VERSION 0x00000001
#define FVM_SLICE_FREE 0
#define FVM_BLOCK_SIZE 8192lu
#define FVM_GUID_LEN GPT_GUID_LEN
#define FVM_GUID_STRLEN GPT_GUID_STRLEN
#define FVM_NAME_LEN 24

#ifdef __cplusplus

#include <fbl/algorithm.h>

namespace fvm {

typedef struct {
    uint64_t magic;
    uint64_t version;
    uint64_t pslice_count; // Slices which can be used by vpartitions
    uint64_t slice_size;   // All sizes in bytes
    uint64_t fvm_partition_size;
    uint64_t vpartition_table_size;
    uint64_t allocation_table_size;
    uint64_t generation;
    uint8_t hash[SHA256_DIGEST_LENGTH];
    uint8_t reserved[0]; // Up to the rest of the block
} fvm_t;

static_assert(sizeof(fvm_t) <= FVM_BLOCK_SIZE, "FVM Superblock too large");

#define FVM_MAX_ENTRIES 1024

// Identifies that the partition is inactive, and should be destroyed on
// reboot (unless activated before rebinding the FVM).
constexpr uint32_t kVPartFlagInactive = 0x00000001;
constexpr uint32_t kVPartAllocateMask = 0x00000001; // All acceptable flags to pass to allocate.

typedef struct {
    void init(const uint8_t* type_, const uint8_t* guid_, uint32_t slices_,
              const char* name_, uint32_t flags_) {
        slices = slices_;
        memcpy(type, type_, FVM_GUID_LEN);
        memcpy(guid, guid_, FVM_GUID_LEN);
        memcpy(name, name_, FVM_NAME_LEN);
        flags = flags_;
    }

    void clear() {
        memset(this, 0, sizeof(*this));
    }

    uint8_t type[FVM_GUID_LEN]; // Mirroring GPT value
    uint8_t guid[FVM_GUID_LEN]; // Mirroring GPT value
    uint32_t slices;            // '0' if unallocated
    uint32_t flags;
    uint8_t name[FVM_NAME_LEN];
} vpart_entry_t;

static_assert(sizeof(vpart_entry_t) == 64, "Unexpected VPart entry size");
static_assert(FVM_BLOCK_SIZE % sizeof(vpart_entry_t) == 0,
              "VPart entries might cross block");
static_assert(sizeof(vpart_entry_t) * FVM_MAX_ENTRIES % FVM_BLOCK_SIZE == 0,
              "VPart entries don't cleanly fit within block");

#define VPART_BITS 16
#define VPART_MAX ((1UL << VPART_BITS) - 1)
#define VSLICE_BITS 32
#define VSLICE_MAX ((1UL << VSLICE_BITS) - 1)
#define RESERVED_BITS 16

#define PSLICE_UNALLOCATED 0

typedef struct slice_entry {
    size_t vpart : VPART_BITS; // '0' if unallocated
    size_t vslice : VSLICE_BITS;
    size_t reserved : RESERVED_BITS;
} __attribute__((packed)) slice_entry_t;

static_assert(FVM_MAX_ENTRIES <= VPART_MAX, "vpart adress space too small");
static_assert(sizeof(slice_entry_t) == 8, "Unexpected FVM slice entry size");
static_assert(FVM_BLOCK_SIZE % sizeof(slice_entry_t) == 0,
              "FVM slice entry might cross block");

constexpr size_t kVPartTableOffset = FVM_BLOCK_SIZE;
constexpr size_t kVPartTableLength = (sizeof(vpart_entry_t) * FVM_MAX_ENTRIES);
constexpr size_t kAllocTableOffset = kVPartTableOffset + kVPartTableLength;

constexpr size_t AllocTableLength(size_t total_size, size_t slice_size) {
    return fbl::round_up(sizeof(slice_entry_t) * (total_size / slice_size),
                         FVM_BLOCK_SIZE);
}

constexpr size_t MetadataSize(size_t total_size, size_t slice_size) {
    return kAllocTableOffset + AllocTableLength(total_size, slice_size);
}

constexpr size_t BackupStart(size_t total_size, size_t slice_size) {
    return MetadataSize(total_size, slice_size);
}

constexpr size_t SlicesStart(size_t total_size, size_t slice_size) {
    return 2 * MetadataSize(total_size, slice_size);
}

constexpr size_t UsableSlicesCount(size_t total_size, size_t slice_size) {
    return (total_size - SlicesStart(total_size, slice_size)) / slice_size;
}

constexpr size_t SliceStart(size_t total_size, size_t slice_size, size_t pslice) {
    return SlicesStart(total_size, slice_size) + (pslice - 1) * slice_size;
}

} // namespace fvm

#endif //  __cplusplus

__BEGIN_CDECLS

// Update's the metadata's hash field to accurately reflect
// the contents of metadata.
void fvm_update_hash(void* metadata, size_t metadata_size);

// Validate the FVM header information, and identify which
// copy of metadata (primary or backup) should be used for
// initial reading, if either.
//
// "out" is an optional output parameter which is equal to a
// valid copy of either metadata or backup on success.
zx_status_t fvm_validate_header(const void* metadata, const void* backup,
                                size_t metadata_size, const void** out);

// Format a block device to be an empty FVM.
zx_status_t fvm_init(int fd, size_t slice_size);
// Queries driver to obtain slice_size, then overwrites and unbinds an FVM
zx_status_t fvm_destroy(const char* path);
// Given the slice_size, overwrites and unbinds an FVM
zx_status_t fvm_overwrite(const char* path, size_t slice_size);

// Allocates a new vpartition in the fvm, and waits for it to become
// accessible (by watching for a corresponding block device).
//
// Returns an open fd to the new partition on success, -1 on error.
int fvm_allocate_partition(int fvm_fd, const alloc_req_t* request);

// TODO(smklein): Move the following function out of ulib/fvm, it is
// also applicable to the GPT

// Waits for a partition with a GUID pair to appear, and opens it.
//
// If one of the GUIDs is null, it is ignored. For example:
//   wait_for_partition(NULL, systemGUID, ZX_SEC(5));
// Waits for any partition with the corresponding system GUID to appear.
// At least one of the GUIDs must be non-null.
//
// Returns an open fd to the partition on success, -1 on error.
int open_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID,
                   zx_duration_t timeout, char* out_path);

// Finds and destroys the partition with the given GUID pair, if it exists.
zx_status_t destroy_partition(const uint8_t* uniqueGUID, const uint8_t* typeGUID);

__END_CDECLS
