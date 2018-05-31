// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bitmap/raw-bitmap.h>
#include <fs/block-txn.h>
#include <fs/trace.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/event.h>

#include "metrics.h"
#endif

#include <minfs/fsck.h>
#include <minfs/minfs.h>

#include "minfs-private.h"

// #define DEBUG_PRINTF
#ifdef DEBUG_PRINTF
#define xprintf(args...) fprintf(stderr, args)
#else
#define xprintf(args...)
#endif

namespace minfs {
namespace {

// Deletes all known slices from an MinFS Partition.
void minfs_free_slices(Bcache* bc, const minfs_info_t* info) {
    if ((info->flags & kMinfsFlagFVM) == 0) {
        return;
    }
#ifdef __Fuchsia__
    extend_request_t request;
    const size_t kBlocksPerSlice = info->slice_size / kMinfsBlockSize;
    if (info->ibm_slices) {
        request.length = info->ibm_slices;
        request.offset = kFVMBlockInodeBmStart / kBlocksPerSlice;
        bc->FVMShrink(&request);
    }
    if (info->abm_slices) {
        request.length = info->abm_slices;
        request.offset = kFVMBlockDataBmStart / kBlocksPerSlice;
        bc->FVMShrink(&request);
    }
    if (info->ino_slices) {
        request.length = info->ino_slices;
        request.offset = kFVMBlockInodeStart / kBlocksPerSlice;
        bc->FVMShrink(&request);
    }
    if (info->dat_slices) {
        request.length = info->dat_slices;
        request.offset = kFVMBlockDataStart / kBlocksPerSlice;
        bc->FVMShrink(&request);
    }
#endif
}

}  // namespace

void minfs_dump_info(const minfs_info_t* info) {
    xprintf("minfs: data blocks:  %10u (size %u)\n", info->block_count, info->block_size);
    xprintf("minfs: inodes:  %10u (size %u)\n", info->inode_count, info->inode_size);
    xprintf("minfs: allocated blocks  @ %10u\n", info->alloc_block_count);
    xprintf("minfs: allocated inodes  @ %10u\n", info->alloc_inode_count);
    xprintf("minfs: inode bitmap @ %10u\n", info->ibm_block);
    xprintf("minfs: alloc bitmap @ %10u\n", info->abm_block);
    xprintf("minfs: inode table  @ %10u\n", info->ino_block);
    xprintf("minfs: data blocks  @ %10u\n", info->dat_block);
    xprintf("minfs: FVM-aware: %s\n", (info->flags & kMinfsFlagFVM) ? "YES" : "NO");
}

void minfs_dump_inode(const minfs_inode_t* inode, ino_t ino) {
    xprintf("inode[%u]: magic:  %10u\n", ino, inode->magic);
    xprintf("inode[%u]: size:   %10u\n", ino, inode->size);
    xprintf("inode[%u]: blocks: %10u\n", ino, inode->block_count);
    xprintf("inode[%u]: links:  %10u\n", ino, inode->link_count);
}

zx_status_t minfs_check_info(const minfs_info_t* info, Bcache* bc) {
    uint32_t max = bc->Maxblk();
    minfs_dump_info(info);

    if ((info->magic0 != kMinfsMagic0) ||
        (info->magic1 != kMinfsMagic1)) {
        FS_TRACE_ERROR("minfs: bad magic\n");
        return ZX_ERR_INVALID_ARGS;
    }
    if (info->version != kMinfsVersion) {
        FS_TRACE_ERROR("minfs: FS Version: %08x. Driver version: %08x\n", info->version,
              kMinfsVersion);
        return ZX_ERR_INVALID_ARGS;
    }
    if ((info->block_size != kMinfsBlockSize) ||
        (info->inode_size != kMinfsInodeSize)) {
        FS_TRACE_ERROR("minfs: bsz/isz %u/%u unsupported\n", info->block_size, info->inode_size);
        return ZX_ERR_INVALID_ARGS;
    }
    if ((info->flags & kMinfsFlagFVM) == 0) {
        if (info->dat_block + info->block_count > max) {
            FS_TRACE_ERROR("minfs: too large for device\n");
            return ZX_ERR_INVALID_ARGS;
        }
    } else {
        const size_t kBlocksPerSlice = info->slice_size / kMinfsBlockSize;
#ifdef __Fuchsia__
        fvm_info_t fvm_info;
        if (bc->FVMQuery(&fvm_info) != ZX_OK) {
            FS_TRACE_ERROR("minfs: Unable to query FVM\n");
            return ZX_ERR_UNAVAILABLE;
        }

        if (info->slice_size != fvm_info.slice_size) {
            FS_TRACE_ERROR("minfs: Slice size did not match expected\n");
            return ZX_ERR_BAD_STATE;
        }

        size_t expected_count[4];
        expected_count[0] = info->ibm_slices;
        expected_count[1] = info->abm_slices;
        expected_count[2] = info->ino_slices;
        expected_count[3] = info->dat_slices;

        query_request_t request;
        request.count = 4;
        request.vslice_start[0] = kFVMBlockInodeBmStart / kBlocksPerSlice;
        request.vslice_start[1] = kFVMBlockDataBmStart / kBlocksPerSlice;
        request.vslice_start[2] = kFVMBlockInodeStart / kBlocksPerSlice;
        request.vslice_start[3] = kFVMBlockDataStart / kBlocksPerSlice;

        query_response_t response;

        if (bc->FVMVsliceQuery(&request, &response) != ZX_OK) {
            FS_TRACE_ERROR("minfs: Unable to query FVM\n");
            return ZX_ERR_UNAVAILABLE;
        }

        if (response.count != request.count) {
            FS_TRACE_ERROR("minfs: Unable to query FVM\n");
            return ZX_ERR_BAD_STATE;
        }

        for (unsigned i = 0; i < request.count; i++) {
            size_t minfs_count = expected_count[i];
            size_t fvm_count = response.vslice_range[i].count;

            if (!response.vslice_range[i].allocated || fvm_count < minfs_count) {
                // Currently, since Minfs can only grow new slices, it should not be possible for
                // the FVM to report a slice size smaller than what is reported by Minfs. In this
                // case, automatically fail without trying to resolve the situation, as it is
                // possible that Minfs structures are allocated in the slices that have been lost.
                FS_TRACE_ERROR("minfs: Mismatched slice count\n");
                return ZX_ERR_IO_DATA_INTEGRITY;
            }

            if (fvm_count > minfs_count) {
                // If FVM reports more slices than we expect, try to free remainder.
                extend_request_t shrink;
                shrink.length = fvm_count - minfs_count;
                shrink.offset = request.vslice_start[i] + minfs_count;
                zx_status_t status;
                if ((status = bc->FVMShrink(&shrink)) != ZX_OK) {
                    FS_TRACE_ERROR("minfs: Unable to shrink to expected size, status: %d\n",
                                   status);
                    return ZX_ERR_IO_DATA_INTEGRITY;
                }
            }
        }
#endif
        // Verify that the allocated slices are sufficient to hold
        // the allocated data structures of the filesystem.
        size_t ibm_blocks_needed = (info->inode_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
        size_t ibm_blocks_allocated = info->ibm_slices * kBlocksPerSlice;
        if (ibm_blocks_needed > ibm_blocks_allocated) {
            FS_TRACE_ERROR("minfs: Not enough slices for inode bitmap\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (ibm_blocks_allocated + info->ibm_block >= info->abm_block) {
            FS_TRACE_ERROR("minfs: Inode bitmap collides into block bitmap\n");
            return ZX_ERR_INVALID_ARGS;
        }
        size_t abm_blocks_needed = (info->block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
        size_t abm_blocks_allocated = info->abm_slices * kBlocksPerSlice;
        if (abm_blocks_needed > abm_blocks_allocated) {
            FS_TRACE_ERROR("minfs: Not enough slices for block bitmap\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (abm_blocks_allocated + info->abm_block >= info->ino_block) {
            FS_TRACE_ERROR("minfs: Block bitmap collides with inode table\n");
            return ZX_ERR_INVALID_ARGS;
        }
        size_t ino_blocks_needed = (info->inode_count + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
        size_t ino_blocks_allocated = info->ino_slices * kBlocksPerSlice;
        if (ino_blocks_needed > ino_blocks_allocated) {
            FS_TRACE_ERROR("minfs: Not enough slices for inode table\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (ino_blocks_allocated + info->ino_block >= info->dat_block) {
            FS_TRACE_ERROR("minfs: Inode table collides with data blocks\n");
            return ZX_ERR_INVALID_ARGS;
        }
        size_t dat_blocks_needed = info->block_count;
        size_t dat_blocks_allocated = info->dat_slices * kBlocksPerSlice;
        if (dat_blocks_needed > dat_blocks_allocated) {
            FS_TRACE_ERROR("minfs: Not enough slices for data blocks\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (dat_blocks_allocated + info->dat_block >
                   fbl::numeric_limits<blk_t>::max()) {
            FS_TRACE_ERROR("minfs: Data blocks overflow blk_t\n");
            return ZX_ERR_INVALID_ARGS;
        } else if (dat_blocks_needed <= 1) {
            FS_TRACE_ERROR("minfs: Not enough data blocks\n");
            return ZX_ERR_INVALID_ARGS;
        }
    }
    //TODO: validate layout
    return 0;
}

void Minfs::InodeSync(WriteTxn* txn, ino_t ino, const minfs_inode_t* inode) {
    // Obtain the offset of the inode within its containing block
    const uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
    const blk_t inoblock_rel = ino / kMinfsInodesPerBlock;
    const blk_t inoblock_abs = inoblock_rel + Info().ino_block;
    assert(inoblock_abs < kFVMBlockDataStart);
#ifdef __Fuchsia__
    void* inodata = (void*)((uintptr_t)(inode_table_->GetData()) +
                            (uintptr_t)(inoblock_rel * kMinfsBlockSize));
    memcpy((void*)((uintptr_t)inodata + off_of_ino), inode, kMinfsInodeSize);
    txn->Enqueue(inode_table_->GetVmo(), inoblock_rel, inoblock_abs, 1);
#else
    // Since host-side tools don't have "mapped vmos", just read / update /
    // write the single absolute indoe block.
    uint8_t inodata[kMinfsBlockSize];
    bc_->Readblk(inoblock_abs, inodata);
    memcpy((void*)((uintptr_t)inodata + off_of_ino), inode, kMinfsInodeSize);
    bc_->Writeblk(inoblock_abs, inodata);
#endif
}

zx_status_t Minfs::CreateWork(fbl::unique_ptr<WritebackWork>* out) {
    fbl::AllocChecker ac;
    out->reset(new (&ac) WritebackWork(bc_.get()));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    return ZX_OK;
}

#ifdef __Fuchsia__
void Minfs::Sync(SyncCallback closure) {
    fbl::unique_ptr<WritebackWork> wb(new WritebackWork(bc_.get()));
    wb->SetClosure(fbl::move(closure));
    EnqueueWork(fbl::move(wb));
}
#endif

Minfs::Minfs(fbl::unique_ptr<Bcache> bc, const minfs_info_t* info) : bc_(fbl::move(bc)) {
    memcpy(&info_, info, sizeof(minfs_info_t));

#ifndef __Fuchsia__
    if (bc_->extent_lengths_.size() > 0) {
        ZX_ASSERT(bc_->extent_lengths_.size() == EXTENT_COUNT);
        ibm_block_count_ = bc_->extent_lengths_[1] / kMinfsBlockSize;
        abm_block_count_ = bc_->extent_lengths_[2] / kMinfsBlockSize;
        ino_block_count_ = bc_->extent_lengths_[3] / kMinfsBlockSize;
        dat_block_count_ = bc_->extent_lengths_[4] / kMinfsBlockSize;

        ibm_start_block_ = bc_->extent_lengths_[0] / kMinfsBlockSize;
        abm_start_block_ = ibm_start_block_ + ibm_block_count_;
        ino_start_block_ = abm_start_block_ + abm_block_count_;
        dat_start_block_ = ino_start_block_ + ino_block_count_;
    } else {
        ibm_start_block_ = Info().ibm_block;
        abm_start_block_ = Info().abm_block;
        ino_start_block_ = Info().ino_block;
        dat_start_block_ = Info().dat_block;

        ibm_block_count_ = abm_start_block_ - ibm_start_block_;
        abm_block_count_ = ino_start_block_ - abm_start_block_;
        ino_block_count_ = dat_start_block_ - ino_start_block_;
        dat_block_count_ = Info().block_count;
    }
#endif
}

Minfs::~Minfs() {
    vnode_hash_.clear();
}

zx_status_t Minfs::InoFree(VnodeMinfs* vn, WritebackWork* wb) {
    TRACE_DURATION("minfs", "Minfs::InoFree", "ino", vn->ino_);

    inode_allocator_.Free(wb, vn->ino_);
    uint32_t block_count = vn->inode_.block_count;

    // release all direct blocks
    for (unsigned n = 0; n < kMinfsDirect; n++) {
        if (vn->inode_.dnum[n] == 0) {
            continue;
        }
        ValidateBno(vn->inode_.dnum[n]);
        block_count--;
        block_allocator_.Free(wb, vn->inode_.dnum[n]);
    }

    // release all indirect blocks
    for (unsigned n = 0; n < kMinfsIndirect; n++) {
        if (vn->inode_.inum[n] == 0) {
            continue;
        }

#ifdef __Fuchsia__
        zx_status_t status;
        if ((status = vn->InitIndirectVmo()) != ZX_OK) {
            return status;
        }

        uint32_t* entry;
        vn->ReadIndirectVmoBlock(n, &entry);
#else
        uint32_t entry[kMinfsBlockSize];
        vn->ReadIndirectBlock(vn->inode_.inum[n], entry);
#endif

        // release the direct blocks pointed at by the entries in the indirect block
        for (unsigned m = 0; m < kMinfsDirectPerIndirect; m++) {
            if (entry[m] == 0) {
                continue;
            }
            block_count--;
            block_allocator_.Free(wb, entry[m]);
        }
        // release the direct block itself
        block_count--;
        block_allocator_.Free(wb, vn->inode_.inum[n]);
    }

    // release doubly indirect blocks
    for (unsigned n = 0; n < kMinfsDoublyIndirect; n++) {
        if (vn->inode_.dinum[n] == 0) {
            continue;
        }
#ifdef __Fuchsia__
        zx_status_t status;
        if ((status = vn->InitIndirectVmo()) != ZX_OK) {
            return status;
        }

        uint32_t* dentry;
        vn->ReadIndirectVmoBlock(GetVmoOffsetForDoublyIndirect(n), &dentry);
#else
        uint32_t dentry[kMinfsBlockSize];
        vn->ReadIndirectBlock(vn->inode_.dinum[n], dentry);
#endif
        // release indirect blocks
        for (unsigned m = 0; m < kMinfsDirectPerIndirect; m++) {
            if (dentry[m] == 0) {
                continue;
            }

#ifdef __Fuchsia__
            if ((status = vn->LoadIndirectWithinDoublyIndirect(n)) != ZX_OK) {
                return status;
            }

            uint32_t* entry;
            vn->ReadIndirectVmoBlock(GetVmoOffsetForIndirect(n) + m, &entry);

#else
            uint32_t entry[kMinfsBlockSize];
            vn->ReadIndirectBlock(dentry[m], entry);
#endif

            // release direct blocks
            for (unsigned k = 0; k < kMinfsDirectPerIndirect; k++) {
                if (entry[k] == 0) {
                    continue;
                }

                block_count--;
                block_allocator_.Free(wb, entry[k]);
            }

            block_count--;
            block_allocator_.Free(wb, dentry[m]);
        }

        // release the doubly indirect block itself
        block_count--;
        block_allocator_.Free(wb, vn->inode_.dinum[n]);
    }

    ZX_DEBUG_ASSERT(block_count == 0);
    ZX_DEBUG_ASSERT(vn->IsUnlinked());
    return ZX_OK;
}

zx_status_t Minfs::AddInodes(WriteTxn* txn, size_t* out_inodes) {
    TRACE_DURATION("minfs", "Minfs::AddInodes");
#ifdef __Fuchsia__
    if ((Info().flags & kMinfsFlagFVM) == 0) {
        return ZX_ERR_NO_SPACE;
    }

    const size_t kBlocksPerSlice = Info().slice_size / kMinfsBlockSize;
    extend_request_t request;
    request.length = 1;
    request.offset = (kFVMBlockInodeStart / kBlocksPerSlice) + Info().ino_slices;

    const uint32_t kInodesPerSlice = static_cast<uint32_t>(Info().slice_size /
                                                           kMinfsInodeSize);
    uint32_t inodes = (Info().ino_slices + static_cast<uint32_t>(request.length))
            * kInodesPerSlice;
    uint32_t ibmblks = (inodes + kMinfsBlockBits - 1) / kMinfsBlockBits;
    uint32_t ibmblks_old = (Info().inode_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
    ZX_DEBUG_ASSERT(ibmblks_old <= ibmblks);
    if (ibmblks > kBlocksPerSlice) {
        // TODO(smklein): Increase the size of the inode bitmap,
        // in addition to the size of the inode table.
        fprintf(stderr, "Minfs::AddInodes needs to increase inode bitmap size\n");
        return ZX_ERR_NO_SPACE;
    }

    if (bc_->FVMExtend(&request) != ZX_OK) {
        fprintf(stderr, "Minfs::AddInodes FVM Extend failure\n");
        return ZX_ERR_NO_SPACE;
    }

    // Update the inode table
    uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    if (inode_table_->Grow(inoblks * kMinfsBlockSize) != ZX_OK) {
        return ZX_ERR_NO_SPACE;
    }

    info_.ino_slices += static_cast<uint32_t>(request.length);
    info_.inode_count = inodes;
    WriteInfo(txn);
    *out_inodes = inodes;
    return ZX_OK;
#else
    return ZX_ERR_NO_SPACE;
#endif
}

zx_status_t Minfs::AddBlocks(WriteTxn* txn, size_t* out_blocks) {
    TRACE_DURATION("minfs", "Minfs::AddBlocks");
#ifdef __Fuchsia__
    if ((Info().flags & kMinfsFlagFVM) == 0) {
        return ZX_ERR_NO_SPACE;
    }

    const size_t kBlocksPerSlice = Info().slice_size / kMinfsBlockSize;
    extend_request_t request;
    request.length = 1;
    request.offset = (kFVMBlockDataStart / kBlocksPerSlice) + Info().dat_slices;
    uint64_t blocks = (Info().dat_slices + request.length) * kBlocksPerSlice;
    ZX_DEBUG_ASSERT(blocks <= fbl::numeric_limits<uint32_t>::max());
    uint32_t abmblks = (static_cast<uint32_t>(blocks) + kMinfsBlockBits - 1) / kMinfsBlockBits;
    uint32_t abmblks_old = (Info().block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
    ZX_DEBUG_ASSERT(abmblks_old <= abmblks);

    if (abmblks > kBlocksPerSlice) {
        // TODO(smklein): Increase the size of the block bitmap.
        fprintf(stderr, "Minfs::AddBlocks needs to increase block bitmap size\n");
        return ZX_ERR_NO_SPACE;
    }

    if (bc_->FVMExtend(&request) != ZX_OK) {
        fprintf(stderr, "Minfs::AddBlocks FVM Extend failure\n");
        return ZX_ERR_NO_SPACE;
    }

    info_.dat_slices += static_cast<uint32_t>(request.length);
    info_.block_count = static_cast<uint32_t>(blocks);

    WriteInfo(txn);
    *out_blocks = blocks;
    return ZX_OK;
#else
    return ZX_ERR_NO_SPACE;
#endif
}

#ifdef __Fuchsia__
zx_status_t Minfs::CreateFsId() {
    ZX_DEBUG_ASSERT(!fs_id_);
    zx::event event;
    zx_status_t status = zx::event::create(0, &event);
    if (status != ZX_OK) {
        return status;
    }
    zx_info_handle_basic_t info;
    status = event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
        return status;
    }

    fs_id_ = info.koid;
    return ZX_OK;
}
#endif

zx_status_t Minfs::InoNew(WriteTxn* txn, const minfs_inode_t* inode, ino_t* out_ino) {
    zx_status_t status;
    size_t allocated_ino;
    if ((status = inode_allocator_.Allocate(txn, 0, &allocated_ino)) != ZX_OK) {
        return status;
    }
    *out_ino = static_cast<ino_t>(allocated_ino);
    // Write the inode back to storage.
    InodeSync(txn, *out_ino, inode);
    return ZX_OK;
}

zx_status_t Minfs::VnodeNew(WritebackWork* wb, fbl::RefPtr<VnodeMinfs>* out, uint32_t type) {
    TRACE_DURATION("minfs", "Minfs::VnodeNew");
    if ((type != kMinfsTypeFile) && (type != kMinfsTypeDir)) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<VnodeMinfs> vn;
    zx_status_t status;

    // Allocate the in-memory vnode
    if ((status = VnodeMinfs::Allocate(this, type, &vn)) != ZX_OK) {
        return status;
    }

    // Allocate the on-disk inode
    ino_t ino;
    if ((status = InoNew(wb, vn->GetInode(), &ino)) != ZX_OK) {
        return status;
    }
    vn->SetIno(ino);
    VnodeInsert(vn.get());
    *out = fbl::move(vn);
    return 0;
}

void Minfs::VnodeInsert(VnodeMinfs* vn) {
#ifdef __Fuchsia__
    fbl::AutoLock lock(&hash_lock_);
#endif
    ZX_DEBUG_ASSERT_MSG(!vnode_hash_.find(vn->GetKey()).IsValid(), "ino %u already in map\n",
                        vn->GetKey());
    vnode_hash_.insert(vn);
}

fbl::RefPtr<VnodeMinfs> Minfs::VnodeLookup(uint32_t ino) {
#ifdef __Fuchsia__
    fbl::RefPtr<VnodeMinfs> vn;
    {
        // Avoid releasing a reference to |vn| while holding |hash_lock_|.
        fbl::AutoLock lock(&hash_lock_);
        auto rawVn = vnode_hash_.find(ino);
        if (!rawVn.IsValid()) {
            // Nothing exists in the lookup table
            return nullptr;
        }
        vn = fbl::internal::MakeRefPtrUpgradeFromRaw(rawVn.CopyPointer(), hash_lock_);
        if (vn == nullptr) {
            // The vn 'exists' in the map, but it is being deleted.
            // Remove it (by key) so the next person doesn't trip on it,
            // and so we can insert another node with the same key into the hash
            // map.
            // Notably, VnodeRelease erases the vnode by object, not key,
            // so it will not attempt to replace any distinct Vnodes that happen
            // to be re-using the same inode.
            vnode_hash_.erase(ino);
        }
    }
    if (vn != nullptr && vn->IsUnlinked()) {
        vn = nullptr;
    }
    return vn;
#else
    return fbl::WrapRefPtr(vnode_hash_.find(ino).CopyPointer());
#endif
}

void Minfs::VnodeRelease(VnodeMinfs* vn) {
#ifdef __Fuchsia__
    fbl::AutoLock lock(&hash_lock_);
#endif
    vnode_hash_.erase(*vn);
}

zx_status_t Minfs::VnodeGet(fbl::RefPtr<VnodeMinfs>* out, ino_t ino) {
    TRACE_DURATION("minfs", "Minfs::VnodeGet", "ino", ino);
    if ((ino < 1) || (ino >= Info().inode_count)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    fs::Ticker ticker(StartTicker());

    fbl::RefPtr<VnodeMinfs> vn = VnodeLookup(ino);
    if (vn != nullptr) {
        *out = fbl::move(vn);
        UpdateOpenMetrics(/* cache_hit= */ true, ticker.End());
        return ZX_OK;
    }

    // obtain the block of the inode table we need
    uint32_t off_of_ino = (ino % kMinfsInodesPerBlock) * kMinfsInodeSize;
#ifdef __Fuchsia__
    void* inodata = (void*)((uintptr_t)(inode_table_->GetData()) +
                            (uintptr_t)((ino / kMinfsInodesPerBlock) * kMinfsBlockSize));
#else
    uint8_t inodata[kMinfsBlockSize];
    bc_->Readblk(Info().ino_block + (ino / kMinfsInodesPerBlock), inodata);
#endif
    const minfs_inode_t* inode = reinterpret_cast<const minfs_inode_t*>((uintptr_t)inodata +
                                                                        off_of_ino);
    zx_status_t status;
    if ((status = VnodeMinfs::Recreate(this, ino, inode, &vn)) != ZX_OK) {
        return ZX_ERR_NO_MEMORY;
    }

    VnodeInsert(vn.get());

    *out = fbl::move(vn);
    UpdateOpenMetrics(/* cache_hit= */ false, ticker.End());
    return ZX_OK;
}

// Allocate a new data block from the block bitmap.
//
// If hint is nonzero it indicates which block number to start the search for
// free blocks from.
zx_status_t Minfs::BlockNew(WriteTxn* txn, blk_t hint, blk_t* out_bno) {
    zx_status_t status;
    size_t allocated_bno;
    if ((status = block_allocator_.Allocate(txn, hint, &allocated_bno)) != ZX_OK) {
        return status;
    }
    *out_bno = static_cast<blk_t>(allocated_bno);
    return ZX_OK;
}

void Minfs::BlockFree(WriteTxn* txn, blk_t bno) {
    block_allocator_.Free(txn, bno);
}

void Minfs::WriteInfo(WriteTxn* txn) {
#ifdef __Fuchsia__
    void* infodata = info_vmo_->GetData();
    memcpy(infodata, &info_, sizeof(info_));
    txn->Enqueue(info_vmo_->GetVmo(), 0, 0, 1);
#else
    uint8_t blk[kMinfsBlockSize];
    memset(blk, 0, sizeof(blk));
    memcpy(blk, &info_, sizeof(info_));
    txn->Enqueue(&blk[0], 0, 0, 1);
#endif
}

void minfs_dir_init(void* bdata, ino_t ino_self, ino_t ino_parent) {
#define DE0_SIZE DirentSize(1)

    // directory entry for self
    minfs_dirent_t* de = (minfs_dirent_t*)bdata;
    de->ino = ino_self;
    de->reclen = DE0_SIZE;
    de->namelen = 1;
    de->type = kMinfsTypeDir;
    de->name[0] = '.';

    // directory entry for parent
    de = (minfs_dirent_t*)((uintptr_t)bdata + DE0_SIZE);
    de->ino = ino_parent;
    de->reclen = DirentSize(2) | kMinfsReclenLast;
    de->namelen = 2;
    de->type = kMinfsTypeDir;
    de->name[0] = '.';
    de->name[1] = '.';
}

zx_status_t Minfs::Create(fbl::unique_ptr<Bcache> bc, const minfs_info_t* info,
                          fbl::unique_ptr<Minfs>* out) {
    zx_status_t status = minfs_check_info(info, bc.get());
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Minfs::Create failed to check info: %d\n", status);
        return status;
    }

#ifndef __Fuchsia__
    if (bc->extent_lengths_.size() != 0 && bc->extent_lengths_.size() != EXTENT_COUNT) {
        FS_TRACE_ERROR("minfs: invalid number of extents\n");
        return ZX_ERR_INVALID_ARGS;
    }
#endif
    auto fs = fbl::unique_ptr<Minfs>(new Minfs(fbl::move(bc), info));
    Minfs* raw_fs = fs.get();
    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    uint32_t inodes = info->inode_count;
    fs->inoblks_ = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;

    ReadTxn txn(fs->bc_.get());

    // Block Bitmap allocator initialization.
    auto abm_grow_cb = [raw_fs](WriteTxn* txn, size_t* pool_size) {
        return raw_fs->AddBlocks(txn, pool_size);
    };
    auto abm_update_cb = [raw_fs](WriteTxn* txn, size_t used) {
        raw_fs->info_.alloc_block_count = static_cast<uint32_t>(used);
        raw_fs->WriteInfo(txn);
    };

    blk_t abm_start_block = fs->Info().abm_block;
    size_t blocks_used = fs->Info().alloc_block_count;
    size_t total_blocks = fs->Info().block_count;
    if ((status = fs->block_allocator_.Initialize(fs->bc_.get(), &txn, abm_grow_cb,
                                                  abm_update_cb, abm_start_block,
                                                  blocks_used, total_blocks)) != ZX_OK) {
        return status;
    }

    // Inode Bitmap allocator initialization.
    auto ibm_grow_cb = [raw_fs](WriteTxn* txn, size_t* pool_size) {
        return raw_fs->AddInodes(txn, pool_size);
    };
    auto ibm_update_cb = [raw_fs](WriteTxn* txn, size_t used) {
        raw_fs->info_.alloc_inode_count = static_cast<uint32_t>(used);
        raw_fs->WriteInfo(txn);
    };

    blk_t ibm_start_block = fs->Info().ibm_block;
    size_t inodes_used = fs->Info().alloc_inode_count;
    size_t total_inodes = fs->Info().inode_count;
    if ((status = fs->inode_allocator_.Initialize(fs->bc_.get(), &txn, ibm_grow_cb,
                                                  ibm_update_cb, ibm_start_block,
                                                  inodes_used, total_inodes)) != ZX_OK) {
        return status;
    }
#ifdef __Fuchsia__
    // Create the inode table.
    uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    if ((status = MappedVmo::Create(inoblks * kMinfsBlockSize, "minfs-inode-table",
                                    &fs->inode_table_)) != ZX_OK) {
        return status;
    }

    if ((status = fs->bc_->AttachVmo(fs->inode_table_->GetVmo(),
                                     &fs->inode_table_vmoid_)) != ZX_OK) {
        FS_TRACE_ERROR("Minfs::Create failed to attach inode table VMO: %d\n", status);
        return status;
    }

    // Create the info vmo
    if ((status = MappedVmo::Create(kMinfsBlockSize, "minfs-superblock",
                                    &fs->info_vmo_)) != ZX_OK) {
        return status;
    }

    if ((status = fs->bc_->AttachVmo(fs->info_vmo_->GetVmo(),
                                     &fs->info_vmoid_)) != ZX_OK) {
        return status;
    }

    txn.Enqueue(fs->inode_table_vmoid_, 0, fs->Info().ino_block, inoblks);
    txn.Enqueue(fs->info_vmoid_, 0, 0, 1);
    if ((status = txn.Flush()) != ZX_OK) {
        FS_TRACE_ERROR("Minfs::Create failed to read initial blocks: %d\n", status);
        return status;
    }

    fbl::unique_ptr<MappedVmo> buffer;
    // TODO(smklein): Create max buffer size relative to total RAM size.
    constexpr size_t kWriteBufferSize = 64 * (1LU << 20);
    static_assert(kWriteBufferSize % kMinfsBlockSize == 0,
                  "Buffer Size must be a multiple of the MinFS Block Size");
    if ((status = MappedVmo::Create(kWriteBufferSize, "minfs-writeback",
                                    &buffer)) != ZX_OK) {
        return status;
    }

    if ((status = WritebackBuffer::Create(fs->bc_.get(), fbl::move(buffer),
                                          &fs->writeback_)) != ZX_OK) {
        return status;
    }

    status = fs->CreateFsId();
    if (status != ZX_OK) {
        FS_TRACE_ERROR("minfs: failed to create fs_id:%d\n", status);
        return status;
    }

#endif

    *out = fbl::move(fs);
    return ZX_OK;
}

zx_status_t minfs_mount(fbl::unique_ptr<minfs::Bcache> bc, fbl::RefPtr<VnodeMinfs>* root_out) {
    TRACE_DURATION("minfs", "minfs_mount");
    zx_status_t status;

    char blk[kMinfsBlockSize];
    if ((status = bc->Readblk(0, &blk)) != ZX_OK) {
        FS_TRACE_ERROR("minfs: could not read info block\n");
        return status;
    }
    const minfs_info_t* info = reinterpret_cast<minfs_info_t*>(blk);

    fbl::unique_ptr<Minfs> fs;
    if ((status = Minfs::Create(fbl::move(bc), info, &fs)) != ZX_OK) {
        FS_TRACE_ERROR("minfs: mount failed\n");
        return status;
    }

    fbl::RefPtr<VnodeMinfs> vn;
    if ((status = fs->VnodeGet(&vn, kMinfsRootIno)) != ZX_OK) {
        FS_TRACE_ERROR("minfs: cannot find root inode\n");
        return status;
    }

    ZX_DEBUG_ASSERT(vn->IsDirectory());
    __UNUSED auto r = fs.release();
    *root_out = fbl::move(vn);
    return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t MountAndServe(const minfs_options_t* options, async_t* async,
                          fbl::unique_ptr<Bcache> bc, zx::channel mount_channel,
                          fbl::Closure on_unmount) {
    TRACE_DURATION("minfs", "MountAndServe");

    fbl::RefPtr<VnodeMinfs> vn;
    zx_status_t status = minfs_mount(fbl::move(bc), &vn);
    if (status != ZX_OK) {
        return status;
    }

    Minfs* vfs = vn->fs_;
    vfs->SetReadonly(options->readonly);
    vfs->SetMetrics(options->metrics);
    vfs->SetUnmountCallback(fbl::move(on_unmount));
    vfs->SetAsync(async);
    return vfs->ServeDirectory(fbl::move(vn), fbl::move(mount_channel));
}

void Minfs::Shutdown(fs::Vfs::ShutdownCallback cb) {
    ManagedVfs::Shutdown([this, cb = fbl::move(cb)] (zx_status_t status) mutable {
        Sync([this, cb = fbl::move(cb)](zx_status_t) mutable {
            async::PostTask(async(), [this, cb = fbl::move(cb)]() mutable {
                // Ensure writeback buffer completes before auxilliary structures
                // are deleted.
                writeback_ = nullptr;
                bc_->Sync();

                DumpMetrics();

                auto on_unmount = fbl::move(on_unmount_);

                // Explicitly delete this (rather than just letting the memory release when
                // the process exits) to ensure that the block device's fifo has been
                // closed.
                delete this;

                // Identify to the unmounting channel that teardown is complete.
                cb(ZX_OK);

                // Identify to the unmounting thread that teardown is complete.
                if (on_unmount) {
                    on_unmount();
                }
            });
        });
    });
}
#endif

zx_status_t Mkfs(fbl::unique_ptr<Bcache> bc) {
    minfs_info_t info;
    memset(&info, 0x00, sizeof(info));
    info.magic0 = kMinfsMagic0;
    info.magic1 = kMinfsMagic1;
    info.version = kMinfsVersion;
    info.flags = kMinfsFlagClean;
    info.block_size = kMinfsBlockSize;
    info.inode_size = kMinfsInodeSize;

    uint32_t blocks = 0;
    uint32_t inodes = 0;

    zx_status_t status;
    auto fvm_cleanup = fbl::MakeAutoCall([bc = bc.get(), &info](){
        minfs_free_slices(bc, &info);
    });
#ifdef __Fuchsia__
    fvm_info_t fvm_info;
    if (bc->FVMQuery(&fvm_info) == ZX_OK) {
        info.slice_size = fvm_info.slice_size;
        info.flags |= kMinfsFlagFVM;


        if (info.slice_size % kMinfsBlockSize) {
            fprintf(stderr, "minfs mkfs: Slice size not multiple of minfs block\n");
            return -1;
        }

        const size_t kBlocksPerSlice = info.slice_size / kMinfsBlockSize;
        extend_request_t request;
        request.length = 1;
        request.offset = kFVMBlockInodeBmStart / kBlocksPerSlice;
        if ((status = bc->FVMReset()) != ZX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to reset FVM slices: %d\n", status);
            return status;
        }
        if ((status = bc->FVMExtend(&request)) != ZX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to allocate inode bitmap: %d\n", status);
            return status;
        }
        info.ibm_slices = 1;
        request.offset = kFVMBlockDataBmStart / kBlocksPerSlice;
        if ((status = bc->FVMExtend(&request)) != ZX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to allocate data bitmap: %d\n", status);
            return status;
        }
        info.abm_slices = 1;
        request.offset = kFVMBlockInodeStart / kBlocksPerSlice;
        if ((status = bc->FVMExtend(&request)) != ZX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to allocate inode table: %d\n", status);
            return status;
        }
        info.ino_slices = 1;
        request.offset = kFVMBlockDataStart / kBlocksPerSlice;
        if ((status = bc->FVMExtend(&request)) != ZX_OK) {
            fprintf(stderr, "minfs mkfs: Failed to allocate data blocks\n");
            return status;
        }
        info.dat_slices = 1;

        inodes = static_cast<uint32_t>(info.ino_slices * info.slice_size / kMinfsInodeSize);
        blocks = static_cast<uint32_t>(info.dat_slices * info.slice_size / kMinfsBlockSize);
    }
#endif
    if ((info.flags & kMinfsFlagFVM) == 0) {
        inodes = kMinfsDefaultInodeCount;
        blocks = bc->Maxblk();
    }

    // determine how many blocks of inodes, allocation bitmaps,
    // and inode bitmaps there are
    uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
    uint32_t ibmblks = (inodes + kMinfsBlockBits - 1) / kMinfsBlockBits;
    uint32_t abmblks;

    info.inode_count = inodes;
    info.alloc_block_count = 0;
    info.alloc_inode_count = 0;
    if ((info.flags & kMinfsFlagFVM) == 0) {
        // Aligning distinct data areas to 8 block groups.
        uint32_t non_dat_blocks = (8 + fbl::round_up(ibmblks, 8u) + inoblks);
        if (non_dat_blocks >= blocks) {
            fprintf(stderr, "mkfs: Partition size (%" PRIu64 " bytes) is too small\n",
                    static_cast<uint64_t>(blocks) * kMinfsBlockSize);
            return ZX_ERR_INVALID_ARGS;
        }

        uint32_t dat_block_count_ = blocks - non_dat_blocks;
        abmblks = (dat_block_count_ + kMinfsBlockBits - 1) / kMinfsBlockBits;
        info.block_count = dat_block_count_ - fbl::round_up(abmblks, 8u);
        info.ibm_block = 8;
        info.abm_block = info.ibm_block + fbl::round_up(ibmblks, 8u);
        info.ino_block = info.abm_block + fbl::round_up(abmblks, 8u);
        info.dat_block = info.ino_block + inoblks;
    } else {
        info.block_count = blocks;
        abmblks = (info.block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
        info.ibm_block = kFVMBlockInodeBmStart;
        info.abm_block = kFVMBlockDataBmStart;
        info.ino_block = kFVMBlockInodeStart;
        info.dat_block = kFVMBlockDataStart;
    }

    minfs_dump_info(&info);

    RawBitmap abm;
    RawBitmap ibm;

    // By allocating the bitmap and then shrinking it, we keep the underlying
    // storage a block multiple but ensure we can't allocate beyond the last
    // real block or inode.
    if ((status = abm.Reset(fbl::round_up(info.block_count, kMinfsBlockBits))) != ZX_OK) {
        FS_TRACE_ERROR("mkfs: Failed to allocate block bitmap\n");
        return status;
    }
    if ((status = ibm.Reset(fbl::round_up(info.inode_count, kMinfsBlockBits))) != ZX_OK) {
        FS_TRACE_ERROR("mkfs: Failed to allocate inode bitmap\n");
        return status;
    }
    if ((status = abm.Shrink(info.block_count)) != ZX_OK) {
        FS_TRACE_ERROR("mkfs: Failed to shrink block bitmap\n");
        return status;
    }
    if ((status = ibm.Shrink(info.inode_count)) != ZX_OK) {
        FS_TRACE_ERROR("mkfs: Failed to shrink inode bitmap\n");
        return status;
    }

    // write rootdir
    uint8_t blk[kMinfsBlockSize];
    memset(blk, 0, sizeof(blk));
    minfs_dir_init(blk, kMinfsRootIno, kMinfsRootIno);
    if ((status = bc->Writeblk(info.dat_block + 1, blk)) != ZX_OK) {
        FS_TRACE_ERROR("mkfs: Failed to write root directory\n");
        return status;
    }

    // update inode bitmap
    ibm.Set(0, 1);
    ibm.Set(kMinfsRootIno, kMinfsRootIno + 1);
    info.alloc_inode_count += 2;

    // update block bitmap:
    // Reserve the 0th data block (as a 'null' value)
    // Reserve the 1st data block (for root directory)
    abm.Set(0, 2);
    info.alloc_block_count += 2;

    // write allocation bitmap
    for (uint32_t n = 0; n < abmblks; n++) {
        void* bmdata = fs::GetBlock<kMinfsBlockSize>(abm.StorageUnsafe()->GetData(), n);
        memcpy(blk, bmdata, kMinfsBlockSize);
        bc->Writeblk(info.abm_block + n, blk);
    }

    // write inode bitmap
    for (uint32_t n = 0; n < ibmblks; n++) {
        void* bmdata = fs::GetBlock<kMinfsBlockSize>(ibm.StorageUnsafe()->GetData(), n);
        memcpy(blk, bmdata, kMinfsBlockSize);
        bc->Writeblk(info.ibm_block + n, blk);
    }

    // write inodes
    memset(blk, 0, sizeof(blk));
    for (uint32_t n = 0; n < inoblks; n++) {
        bc->Writeblk(info.ino_block + n, blk);
    }

    // setup root inode
    minfs_inode_t* ino = reinterpret_cast<minfs_inode_t*>(&blk[0]);
    ino[kMinfsRootIno].magic = kMinfsMagicDir;
    ino[kMinfsRootIno].size = kMinfsBlockSize;
    ino[kMinfsRootIno].block_count = 1;
    ino[kMinfsRootIno].link_count = 2;
    ino[kMinfsRootIno].dirent_count = 2;
    ino[kMinfsRootIno].dnum[0] = 1;
    bc->Writeblk(info.ino_block, blk);

    memset(blk, 0, sizeof(blk));
    memcpy(blk, &info, sizeof(info));
    bc->Writeblk(0, blk);

    fvm_cleanup.cancel();
    return ZX_OK;
}

zx_status_t Minfs::ReadIno(blk_t bno, void* data) {
#ifdef __Fuchsia__
    return bc_->Readblk(Info().ino_block + bno, data);
#else
    return ReadBlk(bno, ino_start_block_, ino_block_count_, inoblks_, data);
#endif
}

zx_status_t Minfs::ReadDat(blk_t bno, void* data) {
#ifdef __Fuchsia__
    return bc_->Readblk(Info().dat_block + bno, data);
#else
    return ReadBlk(bno, dat_start_block_, dat_block_count_, Info().block_count, data);
#endif
}

#ifndef __Fuchsia__
zx_status_t Minfs::ReadBlk(blk_t bno, blk_t start, blk_t soft_max, blk_t hard_max, void* data) {
    if (bno >= hard_max) {
        return ZX_ERR_OUT_OF_RANGE;
    } if (bno >= soft_max) {
        memset(data, 0, kMinfsBlockSize);
        return ZX_OK;
    }

    return bc_->Readblk(start + bno, data);
}

zx_status_t minfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                       const fbl::Vector<size_t>& extent_lengths) {
    if (extent_lengths.size() != EXTENT_COUNT) {
        fprintf(stderr, "error: invalid number of extents\n");
        return ZX_ERR_INVALID_ARGS;
    }

    struct stat s;
    if (fstat(fd.get(), &s) < 0) {
        fprintf(stderr, "error: minfs could not find end of file/device\n");
        return ZX_ERR_IO;
    }

    if (s.st_size < end) {
        fprintf(stderr, "error: invalid file size\n");
        return ZX_ERR_INVALID_ARGS;
    }

    size_t size = (end - start) / minfs::kMinfsBlockSize;

    zx_status_t status;
    fbl::unique_ptr<minfs::Bcache> bc;
    if ((status = minfs::Bcache::Create(&bc, fbl::move(fd), static_cast<uint32_t>(size)))
        != ZX_OK) {
        fprintf(stderr, "error: cannot create block cache\n");
        return status;
    }

    if ((status = bc->SetSparse(start, extent_lengths)) != ZX_OK) {
        fprintf(stderr, "Bcache is already sparse\n");
        return status;
    }

    return minfs_check(fbl::move(bc));
}
#endif


void Minfs::UpdateInitMetrics(uint32_t dnum_count, uint32_t inum_count,
                              uint32_t dinum_count, uint64_t user_data_size,
                              const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.initialized_vmos++;
        metrics_.init_user_data_size += user_data_size;
        metrics_.init_user_data_ticks += duration;
        metrics_.init_dnum_count += dnum_count;
        metrics_.init_inum_count += inum_count;
        metrics_.init_dinum_count += dinum_count;
    }
#endif
}

void Minfs::UpdateLookupMetrics(bool success, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.lookup_calls++;
        metrics_.lookup_calls_success += success ? 1 : 0;
        metrics_.lookup_ticks += duration;
    }
#endif
}

void Minfs::UpdateCreateMetrics(bool success, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.create_calls++;
        metrics_.create_calls_success += success ? 1 : 0;
        metrics_.create_ticks += duration;
    }
#endif
}

void Minfs::UpdateReadMetrics(uint64_t size, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.read_calls++;
        metrics_.read_size += size;
        metrics_.read_ticks += duration;
    }
#endif
}

void Minfs::UpdateWriteMetrics(uint64_t size, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.write_calls++;
        metrics_.write_size += size;
        metrics_.write_ticks += duration;
    }
#endif
}

void Minfs::UpdateTruncateMetrics(const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.truncate_calls++;
        metrics_.truncate_ticks += duration;
    }
#endif
}

void Minfs::UpdateUnlinkMetrics(bool success, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.unlink_calls++;
        metrics_.unlink_calls_success += success ? 1 : 0;
        metrics_.unlink_ticks += duration;
    }
#endif
}

void Minfs::UpdateRenameMetrics(bool success, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.rename_calls++;
        metrics_.rename_calls_success += success ? 1 : 0;
        metrics_.rename_ticks += duration;
    }
#endif
}

void Minfs::UpdateOpenMetrics(bool cache_hit, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.vnodes_opened++;
        metrics_.vnodes_opened_cache_hit += cache_hit ? 1 : 0;
        metrics_.vnode_open_ticks += duration;
    }
#endif
}

void Minfs::DumpMetrics() const {
#ifdef FS_WITH_METRICS
    if (collecting_metrics_) {
        metrics_.Dump();
    }
#endif
}

} // namespace minfs
