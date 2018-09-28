// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <bitmap/raw-bitmap.h>

#include <minfs/allocator.h>
#include <minfs/block-txn.h>

namespace minfs {
namespace {

// Returns the number of blocks necessary to store a pool containing
// |size| bits.
blk_t BitmapBlocksForSize(size_t size) {
    return (static_cast<blk_t>(size) + kMinfsBlockBits - 1) / kMinfsBlockBits;
}

}  // namespace

AllocatorPromise::~AllocatorPromise() {
    if (reserved_ > 0) {
        ZX_DEBUG_ASSERT(allocator_ != nullptr);
        allocator_->Unreserve(reserved_);
    }
}

size_t AllocatorPromise::Allocate(WriteTxn* txn) {
    ZX_DEBUG_ASSERT(allocator_ != nullptr);
    ZX_DEBUG_ASSERT(reserved_ > 0);
    reserved_--;
    return allocator_->Allocate(txn);
}

AllocatorFvmMetadata::AllocatorFvmMetadata() = default;
AllocatorFvmMetadata::AllocatorFvmMetadata(uint32_t* data_slices,
                                           uint32_t* metadata_slices,
                                           uint64_t slice_size) :
        data_slices_(data_slices), metadata_slices_(metadata_slices),
        slice_size_(slice_size) {}
AllocatorFvmMetadata::AllocatorFvmMetadata(AllocatorFvmMetadata&&) = default;
AllocatorFvmMetadata& AllocatorFvmMetadata::operator=(AllocatorFvmMetadata&&) = default;
AllocatorFvmMetadata::~AllocatorFvmMetadata() = default;

uint32_t AllocatorFvmMetadata::UnitsPerSlices(uint32_t slices, uint32_t unit_size) const {
    uint64_t units = (slice_size_ * slices) / unit_size;
    ZX_DEBUG_ASSERT(units <= fbl::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(units);
}

// NOTE: This helper is only intended to be called for
// values of |blocks| which are known to be convertible to slices
// without loss. This is checked by a runtime assertion.
uint32_t AllocatorFvmMetadata::BlocksToSlices(uint32_t blocks) const {
    const size_t kBlocksPerSlice = slice_size_ / kMinfsBlockSize;
    uint32_t slices = static_cast<uint32_t>(blocks / kBlocksPerSlice);
    ZX_DEBUG_ASSERT(UnitsPerSlices(slices, kMinfsBlockSize) == blocks);
    return slices;
}

AllocatorMetadata::AllocatorMetadata() = default;
AllocatorMetadata::AllocatorMetadata(blk_t data_start_block,
                                     blk_t metadata_start_block, bool using_fvm,
                                     AllocatorFvmMetadata fvm,
                                     uint32_t* pool_used, uint32_t* pool_total) :
    data_start_block_(data_start_block), metadata_start_block_(metadata_start_block),
    using_fvm_(using_fvm), fvm_(fbl::move(fvm)),
    pool_used_(pool_used), pool_total_(pool_total) {}
AllocatorMetadata::AllocatorMetadata(AllocatorMetadata&&) = default;
AllocatorMetadata& AllocatorMetadata::operator=(AllocatorMetadata&&) = default;
AllocatorMetadata::~AllocatorMetadata() = default;

Allocator::Allocator(Bcache* bc, Superblock* sb, size_t unit_size, GrowHandler grow_cb,
                     AllocatorMetadata metadata) :
    bc_(bc), sb_(sb), unit_size_(unit_size), grow_cb_(fbl::move(grow_cb)),
    metadata_(fbl::move(metadata)), reserved_(0), hint_(0) {}
Allocator::~Allocator() = default;

zx_status_t Allocator::Create(Bcache* bc, Superblock* sb, fs::ReadTxn* txn, size_t unit_size,
                              GrowHandler grow_cb, AllocatorMetadata metadata,
                              fbl::unique_ptr<Allocator>* out) {
    auto allocator = fbl::unique_ptr<Allocator>(new Allocator(bc, sb, unit_size,
                                                              fbl::move(grow_cb),
                                                              fbl::move(metadata)));
    blk_t pool_blocks = BitmapBlocksForSize(allocator->metadata_.PoolTotal());

    zx_status_t status;
    if ((status = allocator->map_.Reset(pool_blocks * kMinfsBlockBits)) != ZX_OK) {
        return status;
    }
    if ((status = allocator->map_.Shrink(allocator->metadata_.PoolTotal())) != ZX_OK) {
        return status;
    }
#ifdef __Fuchsia__
    vmoid_t map_vmoid;
    if ((status = bc->AttachVmo(allocator->map_.StorageUnsafe()->GetVmo(), &map_vmoid)) != ZX_OK) {
        return status;
    }
    auto data = map_vmoid;
#else
    auto data = allocator->map_.StorageUnsafe()->GetData();
#endif
    txn->Enqueue(data, 0, allocator->metadata_.MetadataStartBlock(), pool_blocks);
    *out = fbl::move(allocator);
    return ZX_OK;
}

zx_status_t Allocator::Reserve(WriteTxn* txn, size_t count,
                               fbl::unique_ptr<AllocatorPromise>* out_promise) {
    if (GetAvailable() < count) {
        // If we do not have enough free elements, attempt to extend the partition.
        zx_status_t status;
        //TODO(planders): Allow Extend to take in count.
        if ((status = Extend(txn)) != ZX_OK) {
            return status;
        }

        ZX_DEBUG_ASSERT(GetAvailable() >= count);
    }

    reserved_ += count;
    (*out_promise).reset(new AllocatorPromise(this, count));
    return ZX_OK;
}

void Allocator::Unreserve(size_t count) {
    ZX_DEBUG_ASSERT(reserved_ >= count);
    reserved_ -= count;
}

size_t Allocator::Allocate(WriteTxn* txn) {
    ZX_DEBUG_ASSERT(reserved_ > 0);
    size_t bitoff_start;
    if (map_.Find(false, hint_, map_.size(), 1, &bitoff_start) != ZX_OK) {
        ZX_ASSERT(map_.Find(false, 0, hint_, 1, &bitoff_start) == ZX_OK);
    }

    ZX_ASSERT(map_.Set(bitoff_start, bitoff_start + 1) == ZX_OK);

    Persist(txn, bitoff_start, 1);
    metadata_.PoolAllocate(1);
    reserved_ -= 1;
    sb_->Write(txn);
    hint_ = bitoff_start + 1;
    return bitoff_start;
}

void Allocator::Free(WriteTxn* txn, size_t index) {
    ZX_DEBUG_ASSERT(map_.Get(index, index + 1));
    map_.Clear(index, index + 1);
    Persist(txn, index, 1);
    metadata_.PoolRelease(1);
    sb_->Write(txn);

    if (index < hint_) {
        hint_ = index;
    }
}

zx_status_t Allocator::Extend(WriteTxn* txn) {
#ifdef __Fuchsia__
    TRACE_DURATION("minfs", "Minfs::Allocator::Extend");
    if (!metadata_.UsingFvm()) {
        return ZX_ERR_NO_SPACE;
    }
    uint32_t data_slices_diff = 1;

    // Determine if we will have enough space in the bitmap slice
    // to grow |data_slices_diff| data slices.

    // How large is the bitmap right now?
    uint32_t bitmap_slices = metadata_.Fvm().MetadataSlices();
    uint32_t bitmap_blocks = metadata_.Fvm().UnitsPerSlices(bitmap_slices, kMinfsBlockSize);

    // How large does the bitmap need to be?
    uint32_t data_slices = metadata_.Fvm().DataSlices();
    uint32_t data_slices_new = data_slices + data_slices_diff;

    uint32_t pool_size = metadata_.Fvm().UnitsPerSlices(data_slices_new,
                                                        static_cast<uint32_t>(unit_size_));
    uint32_t bitmap_blocks_new = BitmapBlocksForSize(pool_size);

    if (bitmap_blocks_new > bitmap_blocks) {
        // TODO(smklein): Grow the bitmap another slice.
        fprintf(stderr, "Minfs allocator needs to increase bitmap size\n");
        return ZX_ERR_NO_SPACE;
    }

    // Make the request to the FVM.
    extend_request_t request;
    request.length = data_slices_diff;
    request.offset = metadata_.Fvm().BlocksToSlices(metadata_.DataStartBlock()) + data_slices;

    zx_status_t status;
    if ((status = bc_->FVMExtend(&request)) != ZX_OK) {
        fprintf(stderr, "minfs::Allocator::Extend failed to grow (on disk): %d\n", status);
        return status;
    }

    if (grow_cb_) {
        if ((status = grow_cb_(pool_size)) != ZX_OK) {
            fprintf(stderr, "minfs::Allocator grow callback failure: %d\n", status);
            return status;
        }
    }

    // Extend the in memory representation of our allocation pool -- it grew!
    ZX_DEBUG_ASSERT(pool_size >= map_.size());
    size_t old_pool_size = map_.size();
    if ((status = map_.Grow(fbl::round_up(pool_size, kMinfsBlockBits))) != ZX_OK) {
        fprintf(stderr, "minfs::Allocator failed to Grow (in memory): %d\n", status);
        return ZX_ERR_NO_SPACE;
    }
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kMinfsBlockSize.
    map_.Shrink(pool_size);

    metadata_.Fvm().SetDataSlices(data_slices_new);
    metadata_.SetPoolTotal(pool_size);
    sb_->Write(txn);

    // Update the block bitmap.
    Persist(txn, old_pool_size, pool_size - old_pool_size);
    return ZX_OK;
#else
    return ZX_ERR_NO_SPACE;
#endif
}

void Allocator::Persist(WriteTxn* txn, size_t index, size_t count) {
    blk_t rel_block = static_cast<blk_t>(index) / kMinfsBlockBits;
    blk_t abs_block = metadata_.MetadataStartBlock() + rel_block;
    blk_t blk_count = BitmapBlocksForSize(count);

#ifdef __Fuchsia__
    auto data = map_.StorageUnsafe()->GetVmo();
#else
    auto data = map_.StorageUnsafe()->GetData();
#endif
    txn->Enqueue(data, rel_block, abs_block, blk_count);
}

} // namespace minfs
