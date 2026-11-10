// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/device/block.h>

// block_op_t's are submitted for processing via the queue() method
// of the block_protocol.  Once submitted, the contents of the block_op_t
// may be modified while it's being processed and/or as it is passed down
// the stack to lower layered drivers.
//
// The contents may be mutated along the way -- for example, a partition
// driver would, after validation, adjust offset_dev to reflect the position
// of the partition.
//
// The completion_cb() must eventually be called upon success or failure and
// at that point the cookie field must contain whatever value was in it when
// the block_op_t was originally queued.
//
// The rw.pages field may be modified but the *contents* of the array it points
// to may not be modified.

typedef struct block_op block_op_t;

// Inline encryption state, defined in <zircon/device/blk_crypto.h>.
struct bio_crypt_ctx;
struct blk_crypto_profile;

// The maximum number of scatter-gather regions in one read or write.
#define BLOCK_OP_MAX_SG_REGIONS 16

// One region of a scatter-gather read or write, in blocks.
typedef struct block_sg_region {
    uint64_t offset_vmo;             // vmo offset in blocks
    uint32_t length;                 // transfer length in blocks
} block_sg_region_t;

struct block_op {
    union {
        // All Commands
        uint32_t command;                // command and flags

        // BLOCK_OP_READ, BLOCK_OP_WRITE
        struct {
            uint32_t command;            // command and flags
            uint32_t extra;              // available for temporary use
            zx_handle_t vmo;             // vmo of data to read or write
            uint32_t length;             // transfer length in blocks (0 is invalid)
            uint64_t offset_dev;         // device offset in blocks
            uint64_t offset_vmo;         // vmo offset in blocks
            uint64_t* pages;             // optional physical page list
            uint64_t sector;             // device offset in 512-byte sectors
            uint64_t sectors;            // transfer length in 512-byte sectors
            bool cache_flushed;          // data cache already maintained
            uint64_t vm;                 // guest physical address of the data
            uint64_t vmo_off;            // vmo offset in bytes
            // Inline encryption context, or NULL for plaintext.
            struct bio_crypt_ctx* crypto_context;
            // If nonzero, sg[] describes the vmo side of the transfer
            // instead of offset_vmo, and length is the sum of its lengths.
            uint32_t sg_count;
            block_sg_region_t sg[BLOCK_OP_MAX_SG_REGIONS];
        } rw;

        // BLOCK_OP_DISCARD, BLOCK_OP_SECURE_ERASE, BLOCK_OP_WRITE_ZEROES
        struct {
            uint32_t command;            // command and flags
            uint32_t length;             // length in blocks
            uint64_t offset_dev;         // device offset in blocks
            uint64_t sector;             // device offset in 512-byte sectors
            uint64_t sectors;            // length in 512-byte sectors
            uint32_t flags;              // request flags from the guest
        } erase;

        // BLOCK_OP_TRIM
        struct {
            uint32_t command;            // command and flags
            // ???
        } trim;
    };

    // The completion_cb() will be called when the block operation
    // succeeds or fails, and cookie will be whatever was set when
    // the block_op was initially queue()'d.
    void (*completion_cb)(block_op_t* block, zx_status_t status);
    void* cookie;
};

static_assert(sizeof(block_op_t) == 368, "");

// Accessors for the inline encryption context of a read or write.
#define block_op_crypto_ctx(bop) ((bop)->rw.crypto_context)
#define block_op_crypto_key(bop) ((bop)->rw.crypto_context->bc_key)
#define block_op_crypto_dun(bop) ((bop)->rw.crypto_context->bc_dun)
#define block_op_crypto_keyslot(bop) ((bop)->rw.crypto_context->keyslot)

typedef struct block_protocol_ops {
    // Obtain the parameters of the block device (block_info_t) and
    // the required size of block_txn_t.  The block_txn_t's submitted
    // via queue() must have block_op_size_out - sizeof(block_op_t) bytes
    // available at the end of the structure for the use of the driver.
    void (*query)(void* ctx, block_info_t* info_out, size_t* block_op_size_out);

    // Submit an IO request for processing.  Success or failure will
    // be reported via the completion_cb() in the block_op_t.  This
    // callback may be called before the queue() method returns.
    void (*queue)(void* ctx, block_op_t* txn);

    // The inline encryption keyslot manager of the device, or NULL if the
    // device has no inline encryption.
    struct blk_crypto_profile* crypto_profile;
} block_protocol_ops_t;

typedef struct block_protocol {
    block_protocol_ops_t* ops;
    void* ctx;
} block_protocol_t;

// Read and Write ops use u.rw for parameters.
//
// If u.rw.pages is not NULL, the VMO is already appropriately pinned
// for IO and pages is an array of the physical addresses covering
// offset_vmo * block_size through (offset_vmo + length + 1U) * block_size.
//
// The number of entries in this array is always
// ((u.rw.length + 1U * block_size + PAGE_SIZE - 1) / PAGE_SIZE)
#define BLOCK_OP_READ                0x00000001
#define BLOCK_OP_WRITE               0x00000002

// Write any controller or device cached data to nonvolatile storage.
// This operation always implies BARRIER_BEFORE and BARRIER_AFTER,
// meaning that previous operations will complete before it starts
// and later operations will not start until it is done.
#define BLOCK_OP_FLUSH               0x00000003

// TBD
#define BLOCK_OP_TRIM                0x00000004

// Discard, write zeroes to, or securely erase a range of blocks.  These ops
// use u.erase for parameters.
#define BLOCK_OP_DISCARD             0x00000005
#define BLOCK_OP_SECURE_ERASE        0x00000006
#define BLOCK_OP_WRITE_ZEROES        0x00000007

#define BLOCK_OP_MASK                0x000000FF


// Mark this operation as "Force Unit Access" (FUA), indicating that
// it should not complete until the data is written to the non-volatile
// medium (write), and that reads should bypass any on-device caches.
#define BLOCK_FL_FORCE_ACCESS        0x00001000

// Require that this operation will not begin until all previous
// operations have completed.
//
// Prevents earlier operations from being reordered after this one.
#define BLOCK_FL_BARRIER_BEFORE      0x00000100

// Require that this operation complete before any subsequent
// operations are started.
//
// Prevents later operations from being reordered before this one.
#define BLOCK_FL_BARRIER_AFTER       0x00000200

// Returns the name of a BLOCK_OP_* command, for log messages.
static inline const char* block_op_str(uint32_t op) {
    switch (op & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
        return "read";
    case BLOCK_OP_WRITE:
        return "write";
    case BLOCK_OP_FLUSH:
        return "flush";
    case BLOCK_OP_TRIM:
        return "trim";
    case BLOCK_OP_DISCARD:
        return "discard";
    case BLOCK_OP_SECURE_ERASE:
        return "secure erase";
    case BLOCK_OP_WRITE_ZEROES:
        return "write zeroes";
    default:
        return "unknown";
    }
}

// Prints a block_op_t, for diagnosing a failed request.
static inline void dump_bop(const block_op_t* bop) {
    printf("block_op %p: command 0x%x\n", (const void*)bop, bop->command);
    switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
        printf("  rw: vmo %u length %u offset_dev %" PRIu64 " offset_vmo %" PRIu64
               " sector %" PRIu64 " sectors %" PRIu64 " vm 0x%" PRIx64 " vmo_off 0x%" PRIx64
               " sg_count %u crypto %p\n",
               bop->rw.vmo, bop->rw.length, bop->rw.offset_dev, bop->rw.offset_vmo,
               bop->rw.sector, bop->rw.sectors, bop->rw.vm, bop->rw.vmo_off,
               bop->rw.sg_count, (const void*)bop->rw.crypto_context);
        for (uint32_t i = 0; i < bop->rw.sg_count && i < BLOCK_OP_MAX_SG_REGIONS; i++) {
            printf("  sg[%u]: offset_vmo %" PRIu64 " length %u\n", i,
                   bop->rw.sg[i].offset_vmo, bop->rw.sg[i].length);
        }
        break;
    case BLOCK_OP_DISCARD:
    case BLOCK_OP_SECURE_ERASE:
    case BLOCK_OP_WRITE_ZEROES:
        printf("  erase: length %u offset_dev %" PRIu64 " sector %" PRIu64
               " sectors %" PRIu64 " flags 0x%x\n",
               bop->erase.length, bop->erase.offset_dev, bop->erase.sector,
               bop->erase.sectors, bop->erase.flags);
        break;
    }
}
