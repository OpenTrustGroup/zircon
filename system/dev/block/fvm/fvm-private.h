// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <ddk/device.h>
#include <fvm/fvm.h>
#include <zircon/device/block.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#ifdef __cplusplus

#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <lib/fzl/mapped-vmo.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

namespace fvm {

class SliceExtent : public fbl::WAVLTreeContainable<fbl::unique_ptr<SliceExtent>> {
public:
    size_t GetKey() const { return vslice_start_; }
    // Vslice start (inclusive)
    size_t start() const { return vslice_start_; }
    // Vslice end (exclusive)
    size_t end() const { return vslice_start_ + pslices_.size(); }
    // Extent length
    size_t size() const { return end() - start(); }
    // Look up a pslice given a vslice
    uint32_t get(size_t vslice) const {
        if (vslice - vslice_start_ >= pslices_.size()) {
            return 0;
        }
        return pslices_[vslice - vslice_start_];
    }

    // Breaks the extent from:
    //   [start(), end())
    // Into:
    //   [start(), vslice] and [vslice + 1, end()).
    // Returns the latter extent on success; returns nullptr
    // if a memory allocation failure occurs.
    fbl::unique_ptr<SliceExtent> Split(size_t vslice);

    // Combines the other extent into this one.
    // 'other' must immediately follow the current slice.
    bool Merge(const SliceExtent& other);

    bool push_back(uint32_t pslice) {
        ZX_DEBUG_ASSERT(pslice != PSLICE_UNALLOCATED);
        fbl::AllocChecker ac;
        pslices_.push_back(pslice, &ac);
        return ac.check();
    }
    void pop_back() { pslices_.pop_back(); }
    bool is_empty() const { return pslices_.size() == 0; }

    SliceExtent(size_t vslice_start)
        : vslice_start_(vslice_start) {}

private:
    friend class TypeWAVLTraits;
    DISALLOW_COPY_ASSIGN_AND_MOVE(SliceExtent);

    fbl::Vector<uint32_t> pslices_;
    const size_t vslice_start_;
};

class VPartitionManager;
using ManagerDeviceType = ddk::Device<VPartitionManager, ddk::Ioctlable, ddk::Unbindable>;

class VPartition;
using PartitionDeviceType = ddk::Device<VPartition,
                                        ddk::Ioctlable,
                                        ddk::GetSizable,
                                        ddk::Unbindable>;

class VPartitionManager : public ManagerDeviceType {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VPartitionManager);
    static zx_status_t Bind(zx_device_t* dev);

    // Read the underlying block device, initialize the recorded VPartitions.
    zx_status_t Load();

    // Block Protocol
    size_t BlockOpSize() const { return block_op_size_; }
    void Queue(block_op_t* txn) const { bp_.ops->queue(bp_.ctx, txn); }

    // Acquire access to a VPart Entry which has already been modified (and
    // will, as a consequence, not be de-allocated underneath us).
    vpart_entry_t* GetAllocatedVPartEntry(size_t index) const TA_NO_THREAD_SAFETY_ANALYSIS {
        auto entry = GetVPartEntryLocked(index);
        ZX_DEBUG_ASSERT(entry->slices > 0);
        return entry;
    }

    // Allocate 'count' slices, write back the FVM.
    zx_status_t AllocateSlices(VPartition* vp, size_t vslice_start, size_t count) TA_EXCL(lock_);

    // Deallocate 'count' slices, write back the FVM.
    // If a request is made to remove vslice_count = 0, deallocates the entire
    // VPartition.
    zx_status_t FreeSlices(VPartition* vp, size_t vslice_start, size_t count) TA_EXCL(lock_);

    // Returns global information about the FVM.
    void Query(fvm_info_t* info) TA_EXCL(lock_);

    size_t DiskSize() const { return info_.block_count * info_.block_size; }
    size_t SliceSize() const { return slice_size_; }
    size_t VSliceMax() const { return VSLICE_MAX; }
    const block_info_t& Info() const { return info_; }

    zx_status_t DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen,
                         void* reply, size_t max, size_t* out_actual);
    void DdkUnbind();
    void DdkRelease();

    VPartitionManager(zx_device_t* dev, const block_info_t& info, size_t block_op_size,
                      const block_protocol_t* bp);
    ~VPartitionManager();

private:
    // Marks the partition with instance GUID |old_guid| as inactive,
    // and marks partitions with instance GUID |new_guid| as active.
    //
    // If a partition with |old_guid| does not exist, it is ignored.
    // If |old_guid| equals |new_guid|, then |old_guid| is ignored.
    // If a partition with |new_guid| does not exist, |ZX_ERR_NOT_FOUND|
    // is returned.
    //
    // Updates the FVM metadata atomically.
    zx_status_t Upgrade(const uint8_t* old_guid, const uint8_t* new_guid) TA_EXCL(lock_);

    // Given a VPartition object, add a corresponding ddk device.
    zx_status_t AddPartition(fbl::unique_ptr<VPartition> vp) const;

    // Update, hash, and write back the current copy of the FVM metadata.
    // Automatically handles alternating writes to primary / backup copy of FVM.
    zx_status_t WriteFvmLocked() TA_REQ(lock_);

    zx_status_t AllocateSlicesLocked(VPartition* vp, size_t vslice_start,
                                     size_t count) TA_REQ(lock_);

    zx_status_t FreeSlicesLocked(VPartition* vp, size_t vslice_start,
                                 size_t count) TA_REQ(lock_);

    zx_status_t FindFreeVPartEntryLocked(size_t* out) const TA_REQ(lock_);
    zx_status_t FindFreeSliceLocked(size_t* out, size_t hint) const TA_REQ(lock_);

    fvm_t* GetFvmLocked() const TA_REQ(lock_) {
        return reinterpret_cast<fvm_t*>(metadata_->GetData());
    }

    // Mark a slice as free in the metadata structure.
    // Update free slice accounting.
    void FreePhysicalSlice(size_t pslice) TA_REQ(lock_);

    // Mark a slice as allocated in the metadata structure.
    // Update allocated slice accounting.
    void AllocatePhysicalSlice(size_t pslice, uint64_t vpart, uint64_t vslice) TA_REQ(lock_);

    // Given a physical slice (acting as an index into the slice table),
    // return the associated slice entry.
    slice_entry_t* GetSliceEntryLocked(size_t index) const TA_REQ(lock_);

    // Given an index into the vpartition table, return the associated
    // virtual partition entry.
    vpart_entry_t* GetVPartEntryLocked(size_t index) const TA_REQ(lock_);

    size_t PrimaryOffsetLocked() const TA_REQ(lock_) {
        return first_metadata_is_primary_ ? 0 : MetadataSize();
    }

    size_t BackupOffsetLocked() const TA_REQ(lock_) {
        return first_metadata_is_primary_ ? MetadataSize() : 0;
    }

    size_t MetadataSize() const {
        return metadata_size_;
    }

    zx_status_t DoIoLocked(zx_handle_t vmo, size_t off, size_t len, uint32_t command);

    thrd_t initialization_thread_;
    block_info_t info_; // Cached info from parent device

    fbl::Mutex lock_;
    fbl::unique_ptr<fzl::MappedVmo> metadata_ TA_GUARDED(lock_);
    bool first_metadata_is_primary_ TA_GUARDED(lock_);
    size_t metadata_size_;
    size_t slice_size_;
    // Number of allocatable slices.
    size_t pslice_total_count_;
    // Number of currently allocated slices.
    size_t pslice_allocated_count_ TA_GUARDED(lock_);

    // Block Protocol
    const size_t block_op_size_;
    block_protocol_t bp_;
};

class VPartition : public PartitionDeviceType, public ddk::BlockProtocol<VPartition> {
public:
    static zx_status_t Create(VPartitionManager* vpm, size_t entry_index,
                              fbl::unique_ptr<VPartition>* out);
    // Device Protocol
    zx_status_t DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen,
                         void* reply, size_t max, size_t* out_actual);
    zx_off_t DdkGetSize();
    void DdkUnbind();
    void DdkRelease();

    // Block Protocol
    void BlockQuery(block_info_t* info_out, size_t* block_op_size_out);
    void BlockQueue(block_op_t* txn);

    auto ExtentBegin() TA_REQ(lock_) {
        return slice_map_.begin();
    }

    // Given a virtual slice, return the physical slice allocated
    // to it. If no slice is allocated, return PSLICE_UNALLOCATED.
    uint32_t SliceGetLocked(size_t vslice) const TA_REQ(lock_);

    // Check slices starting from |vslice_start|.
    // Sets |*count| to the number of contiguous allocated or unallocated slices found.
    // Sets |*allocated| to true if the vslice range is allocated, and false otherwise.
    zx_status_t CheckSlices(size_t vslice_start, size_t* count, bool* allocated) TA_EXCL(lock_);

    zx_status_t SliceSetUnsafe(size_t vslice, uint32_t pslice) TA_NO_THREAD_SAFETY_ANALYSIS {
        return SliceSetLocked(vslice, pslice);
    }
    zx_status_t SliceSetLocked(size_t vslice, uint32_t pslice) TA_REQ(lock_);

    bool SliceCanFree(size_t vslice) const TA_REQ(lock_) {
        auto extent = --slice_map_.upper_bound(vslice);
        return extent.IsValid() && extent->get(vslice) != PSLICE_UNALLOCATED;
    }

    // Returns "true" if slice freed successfully, false otherwise.
    // If freeing from the back of an extent, guaranteed not to fail.
    bool SliceFreeLocked(size_t vslice) TA_REQ(lock_);

    // Destroy the extent containing the vslice.
    void ExtentDestroyLocked(size_t vslice) TA_REQ(lock_);

    size_t BlockSize() const TA_NO_THREAD_SAFETY_ANALYSIS {
        return info_.block_size;
    }
    void AddBlocksLocked(ssize_t nblocks) TA_REQ(lock_) {
        info_.block_count += nblocks;
    }

    size_t GetEntryIndex() const { return entry_index_; }

    void KillLocked() TA_REQ(lock_) { entry_index_ = 0; }
    bool IsKilledLocked() TA_REQ(lock_) { return entry_index_ == 0; }

    VPartition(VPartitionManager* vpm, size_t entry_index, size_t block_op_size);
    ~VPartition();
    fbl::Mutex lock_;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VPartition);

    zx_device_t* GetParent() const { return mgr_->parent(); }

    VPartitionManager* mgr_;
    size_t entry_index_;

    // Mapping of virtual slice number (index) to physical slice number (value).
    // Physical slice zero is reserved to mean "unmapped", so a zeroed slice_map
    // indicates that the vpartition is completely unmapped, and uses no
    // physical slices.
    fbl::WAVLTree<size_t, fbl::unique_ptr<SliceExtent>> slice_map_ TA_GUARDED(lock_);
    block_info_t info_ TA_GUARDED(lock_);
};

} // namespace fvm

#endif // ifdef __cplusplus

__BEGIN_CDECLS

/////////////////// C-compatibility definitions (Provided to C from C++)

// Binds FVM driver to a device; loads the VPartition devices asynchronously in
// a background thread.
zx_status_t fvm_bind(zx_device_t* dev);

__END_CDECLS
