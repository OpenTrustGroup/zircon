// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains Vnodes and global Blobfs structures
// used for constructing a Blobfs filesystem in memory.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <string.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <block-client/client.h>
#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <fs/mapped-vmo.h>
#include <fs/managed-vfs.h>
#include <fs/ticker.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>
#include <trace/event.h>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/metrics.h>
#include <blobfs/writeback.h>

namespace blobfs {

class Blobfs;
class VnodeBlob;
class WriteTxn;
class WritebackWork;

using ReadTxn = fs::ReadTxn<kBlobfsBlockSize, Blobfs>;
using digest::Digest;

typedef uint32_t BlobFlags;

// clang-format off

// After Open;
constexpr BlobFlags kBlobStateEmpty       = 0x00000001; // Not yet allocated
// After Ioctl configuring size:
constexpr BlobFlags kBlobStateDataWrite   = 0x00000002; // Data is being written
// After Writing:
constexpr BlobFlags kBlobStateReadable    = 0x00000004; // Readable
// After Unlink:
constexpr BlobFlags kBlobStatePurged      = 0x00000008; // Blob should be released during recycle
// Unrecoverable error state:
constexpr BlobFlags kBlobStateError       = 0x00000010; // Unrecoverable error state
constexpr BlobFlags kBlobStateMask        = 0x000000FF;

// Informational non-state flags:
constexpr BlobFlags kBlobFlagDeletable    = 0x00000100; // This node should be unlinked when closed
constexpr BlobFlags kBlobFlagDirectory    = 0x00000200; // This node represents the root directory
constexpr BlobFlags kBlobOtherMask        = 0x0000FF00;

// clang-format on

class VnodeBlob final : public fs::Vnode, public fbl::Recyclable<VnodeBlob> {
public:
    // Intrusive methods and structures
    using WAVLTreeNodeState = fbl::WAVLTreeNodeState<VnodeBlob*>;
    struct TypeWavlTraits {
        static WAVLTreeNodeState& node_state(VnodeBlob& b) { return b.type_wavl_state_; }
    };
    const uint8_t* GetKey() const {
        return &digest_[0];
    };

    BlobFlags GetState() const {
        return flags_ & kBlobStateMask;
    }

    bool Purgeable() const {
        return fd_count_ == 0 && (DeletionQueued() || !(GetState() & kBlobStateReadable));
    }

    bool IsDirectory() const { return flags_ & kBlobFlagDirectory; }

    zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                      void* out_buf, size_t out_len, size_t* out_actual) final;

    bool DeletionQueued() const {
        return flags_ & kBlobFlagDeletable;
    }

    void SetState(BlobFlags new_state) {
        flags_ = (flags_ & ~kBlobStateMask) | new_state;
    }

    size_t GetMapIndex() const {
        return map_index_;
    }

    void PopulateInode(size_t node_index);

    uint64_t SizeData() const;

    const blobfs_inode_t& GetNode() const {
        return inode_;
    }

    // Constructs the "directory" blob
    VnodeBlob(Blobfs* bs);
    // Constructs actual blobs
    VnodeBlob(Blobfs* bs, const Digest& digest);

    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Close() final;

    void fbl_recycle() final;
    void TearDown();
    virtual ~VnodeBlob();
    void CompleteSync();

    // Constructs a blob, reads in data, verifies the contents, then destroys the in-memory copy.
    static zx_status_t VerifyBlob(Blobfs* bs, size_t node_index);
private:
    friend struct TypeWavlTraits;

    DISALLOW_COPY_ASSIGN_AND_MOVE(VnodeBlob);

    void BlobCloseHandles();

    // Returns a handle to an event which will be signalled when
    // the blob is readable.
    //
    // Returns "ZX_OK" if blob is already readable.
    // Otherwise, returns size of the handle.
    zx_status_t GetReadableEvent(zx_handle_t* out);

    // Return a clone of the blobfs VMO.
    //
    // Monitors the current VMO, keeping a reference to the Vnode
    // alive while the |out| VMO (and any clones it may have) are open.
    zx_status_t CloneVmo(zx_rights_t rights, zx_handle_t* out);
    void HandleNoClones(async_t* async, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);

    void QueueUnlink();

    void TryPurge() {
        if (Purgeable()) {
            Purge();
        }
    }

    // Verify that the blob is purgeable and remove all traces of the blob from blobfs.
    // The blob is not expected to be accessed again after this is called.
    void Purge();

    // If successful, allocates Blob Node and Blocks (in-memory)
    // kBlobStateEmpty --> kBlobStateDataWrite
    zx_status_t SpaceAllocate(uint64_t size_data);

    // Writes to either the Merkle Tree or the Data section,
    // depending on the state.
    zx_status_t WriteInternal(const void* data, size_t len, size_t* actual);

    // Reads from a blob.
    // Requires: kBlobStateReadable
    zx_status_t ReadInternal(void* data, size_t len, size_t off, size_t* actual);

    // Vnode I/O operations
    zx_status_t GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                           zxrio_object_info_t* extra) final;
    zx_status_t ValidateFlags(uint32_t flags) final;
    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                        size_t* out_actual) final;
    zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
    zx_status_t Write(const void* data, size_t len, size_t offset,
                      size_t* out_actual) final;
    zx_status_t Append(const void* data, size_t len, size_t* out_end,
                       size_t* out_actual) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name,
                       uint32_t mode) final;
    zx_status_t Truncate(size_t len) final;
    zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir) final;
    zx_status_t GetVmo(int flags, zx_handle_t* out) final;
    void Sync(SyncCallback closure) final;

    // Read both VMOs into memory, if we haven't already.
    //
    // TODO(ZX-1481): When we have can register the Blob Store as a pager
    // service, and it can properly handle pages faults on a vnode's contents,
    // then we can avoid reading the entire blob up-front. Until then, read
    // the contents of a VMO into memory when it is opened.
    zx_status_t InitVmos();

    // Verify the integrity of the in-memory Blob.
    // InitVmos() must have already been called for this blob.
    zx_status_t Verify() const;

    void WriteShared(WriteTxn* txn, size_t start, size_t len, uint64_t start_block);

    // Called by Blob once the last write has completed, updating the
    // on-disk metadata.
    zx_status_t WriteMetadata(fbl::unique_ptr<WritebackWork> wb);

    // Acquire a pointer to the mapped data or merkle tree
    void* GetData() const;
    void* GetMerkle() const;

    WAVLTreeNodeState type_wavl_state_ = {};

    Blobfs* const blobfs_;
    BlobFlags flags_ = {};
    fbl::atomic_bool syncing_;

    // The blob_ here consists of:
    // 1) The Merkle Tree
    // 2) The Blob itself, aligned to the nearest kBlobfsBlockSize
    fbl::unique_ptr<MappedVmo> blob_ = {};
    vmoid_t vmoid_ = {};

    // Watches any clones of "blob_" provided to clients.
    // Observes the ZX_VMO_ZERO_CHILDREN signal.
    async::WaitMethod<VnodeBlob, &VnodeBlob::HandleNoClones> clone_watcher_;
    // Keeps a reference to the blob alive (from within itself)
    // until there are no cloned VMOs in used.
    //
    // This RefPtr is only non-null when a client is using a cloned VMO,
    // or there would be a clear leak of VnodeBlob.
    fbl::RefPtr<VnodeBlob> clone_ref_ = {};

    zx::event readable_event_ = {};
    uint64_t bytes_written_ = {};
    uint8_t digest_[Digest::kLength] = {};

    uint32_t fd_count_ = {};
    size_t map_index_ = {};
    blobfs_inode_t inode_ = {};
};

// We need to define this structure to allow the Blob to be indexable by a key
// which is larger than a primitive type: the keys are 'Digest::kLength'
// bytes long.
struct MerkleRootTraits {
    static const uint8_t* GetKey(const VnodeBlob& obj) { return obj.GetKey(); }
    static bool LessThan(const uint8_t* k1, const uint8_t* k2) {
        return memcmp(k1, k2, Digest::kLength) < 0;
    }
    static bool EqualTo(const uint8_t* k1, const uint8_t* k2) {
        return memcmp(k1, k2, Digest::kLength) == 0;
    }
};

class Blobfs : public fs::ManagedVfs, public fbl::RefCounted<Blobfs> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Blobfs);
    friend class VnodeBlob;

    static zx_status_t Create(fbl::unique_fd blockfd, const blobfs_info_t* info,
                              fbl::unique_ptr<Blobfs>* out);

    void CollectMetrics() { collecting_metrics_ = true; }
    bool CollectingMetrics() const { return collecting_metrics_; }
    void DisableMetrics() { collecting_metrics_ = false; }
    void DumpMetrics() const {
        if (collecting_metrics_) {
            metrics_.Dump();
        }
    }

    void SetUnmountCallback(fbl::Closure closure) {
        on_unmount_ = fbl::move(closure);
    }

    // Initializes the WritebackBuffer.
    zx_status_t InitializeWriteback();

    void Shutdown(fs::Vfs::ShutdownCallback closure) final;
    virtual ~Blobfs();

    // Invokes "open" on the root directory.
    // Acts as a special-case to bootstrap filesystem mounting.
    zx_status_t OpenRootNode(fbl::RefPtr<VnodeBlob>* out);

    // Searches for a blob by name.
    // - If a readable blob with the same name exists, return it.
    // - If a blob with the same name exists, but it is not readable,
    //   ZX_ERR_BAD_STATE is returned.
    //
    // 'out' may be null -- the same error code will be returned as if it
    // was a valid pointer.
    //
    // If 'out' is not null, then the blob's  will be added to the
    // "quick lookup" map if it was not there already.
    zx_status_t LookupBlob(const Digest& digest, fbl::RefPtr<VnodeBlob>* out);

    // Creates a new blob in-memory, with no backing disk storage (yet).
    // If a blob with the name already exists, this function fails.
    //
    // Adds Blob to the "quick lookup" map.
    zx_status_t NewBlob(const Digest& digest, fbl::RefPtr<VnodeBlob>* out);

    // Removes blob from 'active' hashmap and deletes all metadata associated with it.
    zx_status_t PurgeBlob(VnodeBlob* blob);

    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual);

    zx_status_t AttachVmo(zx_handle_t vmo, vmoid_t* out);
    zx_status_t Txn(block_fifo_request_t* requests, size_t count) {
        TRACE_DURATION("blobfs", "Blobfs::Txn", "count", count);
        return block_fifo_txn(fifo_client_, requests, count);
    }
    uint32_t BlockSize() const { return block_info_.block_size; }

    txnid_t TxnId() const {
        ZX_DEBUG_ASSERT(blockfd_);
        thread_local txnid_t txnid_ = TXNID_INVALID;
        if (txnid_ != TXNID_INVALID) {
            return txnid_;
        }
        if (ioctl_block_alloc_txn(blockfd_.get(), &txnid_) < 0) {
            return TXNID_INVALID;
        }
        return txnid_;
    }

    void FreeTxnId() {
        txnid_t tid = TxnId();
        if (tid == TXNID_INVALID) {
            return;
        }
        ioctl_block_free_txn(blockfd_.get(), &tid);
    }

    // If possible, attempt to resize the blobfs partition.
    // Add one additional slice for inodes.
    zx_status_t AddInodes();
    // Add enough slices required to hold nblocks additional blocks.
    zx_status_t AddBlocks(size_t nblocks);

    int Fd() const {
        return blockfd_.get();
    }

    // Returns an unique identifier for this instance.
    uint64_t GetFsId() const { return fs_id_; }

    using SyncCallback = fs::Vnode::SyncCallback;
    void Sync(SyncCallback closure);

    // Updates aggregate information about the total number of created
    // blobs since mounting.
    void UpdateAllocationMetrics(uint64_t size_data, const fs::Duration& duration);

    // Updates aggregate information about the number of blobs opened
    // since mounting.
    void UpdateLookupMetrics(uint64_t size);

    // Updates aggregates information about blobs being written back
    // to blobfs since mounting.
    void UpdateClientWriteMetrics(uint64_t data_size, uint64_t merkle_size,
                                  const fs::Duration& enqueue_duration,
                                  const fs::Duration& generate_duration);

    // Updates aggregate information about flushing bits down
    // to the underlying storage driver.
    void UpdateWritebackMetrics(uint64_t size, const fs::Duration& duration);

    // Updates aggregate information about reading blobs from storage
    // since mounting.
    void UpdateMerkleDiskReadMetrics(uint64_t size, const fs::Duration& read_duration,
                                     const fs::Duration& verify_duration);

    // Updates aggregate information about general verification info
    // since mounting.
    void UpdateMerkleVerifyMetrics(uint64_t size_data, uint64_t size_merkle,
                                   const fs::Duration& duration);

    blobfs_info_t info_;

    zx_status_t CreateWork(fbl::unique_ptr<WritebackWork>* out, VnodeBlob* vnode) {
        ZX_DEBUG_ASSERT(writeback_ != nullptr);
        return writeback_->GenerateWork(out, fbl::move(fbl::WrapRefPtr(vnode)));
    }

    void EnqueueWork(fbl::unique_ptr<WritebackWork> work) {
        ZX_DEBUG_ASSERT(writeback_ != nullptr);
        writeback_->Enqueue(fbl::move(work));
    }

    // Does a single pass of all blobs, creating uninitialized Vnode
    // objects for them all.
    //
    // By executing this function at mount, we can quickly assert
    // either the presence or absence of a blob on the system without
    // further scanning.
    zx_status_t InitializeVnodes() __TA_EXCLUDES(hash_lock_);

    // Remove the Vnode without storing it in the closed Vnode cache. This
    // function should be used when purging a blob, as it will prevent
    // additional lookups of VnodeBlob from being made.
    //
    // Precondition: The blob must exist in |open_hash_|.
    void VnodeReleaseHard(VnodeBlob* vn) __TA_EXCLUDES(hash_lock_);

    // Resurrect a Vnode with no strong references, and relocate
    // it from |open_hash_| into |closed_hash_|.
    //
    // Precondition: The blob must exist in the |open_hash_| with
    // no strong references.
    void VnodeReleaseSoft(VnodeBlob* vn) __TA_EXCLUDES(hash_lock_);

private:
    friend class BlobfsChecker;

    Blobfs(fbl::unique_fd fd, const blobfs_info_t* info);
    zx_status_t LoadBitmaps();
    fbl::unique_ptr<WritebackBuffer> writeback_;

    // Inserts a Vnode into the |closed_hash_|, tears down
    // cache Vnode state, and leaks a reference to the Vnode
    // if it was added to the cache successfully.
    //
    // This prevents the vnode from ever being torn down, unless
    // it is re-acquired from |closed_hash_| and released manually
    // (with an identifier to not relocate the Vnode into the cache).
    //
    // Returns an error if the Vnode already exists in the cache.
    zx_status_t VnodeInsertClosedLocked(fbl::RefPtr<VnodeBlob> vn) __TA_REQUIRES(hash_lock_);

    // Upgrades a Vnode which exists in the |closed_hash_| into |open_hash_|,
    // and acquire the strong reference the Vnode which was leaked by
    // |VnodeInsertClosedLocked()|, if it exists.
    //
    // Precondition: The Vnode must not exist in |open_hash_|.
    fbl::RefPtr<VnodeBlob> VnodeUpgradeLocked(const uint8_t* key) __TA_REQUIRES(hash_lock_);

    // Searches for |nblocks| free blocks between the block_map_ and reserved_blocks_ bitmaps.
    zx_status_t FindBlocks(size_t start, size_t nblocks, size_t* blkno_out);

    // Reserves space for a block in memory. Does not update disk.
    zx_status_t ReserveBlocks(size_t nblocks, size_t* blkno_out);

    // Adds reserved blocks to allocated bitmap and writes the bitmap out to disk.
    void PersistBlocks(WriteTxn* txn, size_t nblocks, size_t blkno);

    // Frees blocks from the reserved/allocated maps and updates disk if necessary.
    void FreeBlocks(WriteTxn* txn, size_t nblocks, size_t blkno);

    // Finds an unallocated node between indices start (inclusive) and end (exclusive).
    // If it exists, sets |*node_index_out| to the first available value.
    zx_status_t FindNode(size_t start, size_t end, size_t *node_index_out);

    // Finds and reserves space for a blob node in memory. Does not update disk.
    zx_status_t ReserveNode(size_t* node_index_out);

    // Writes node data to the inode table and updates disk.
    void PersistNode(WriteTxn* txn, size_t node_index, const blobfs_inode_t& inode);

    // Frees a node, from both the reserved map and the inode table. If the inode was allocated
    // in the inode table, write the deleted inode out to disk.
    void FreeNode(WriteTxn* txn, size_t node_index);

    // Returns a reference to the |index|th inode of the node map.
    // This should only be accessed on two occasions:
    // 1. To populate an existing Vnode on Lookup.
    // 2. To update with full contents when a Vnode write has completed.
    // No updates should occur directly on the blobfs's node.
    blobfs_inode_t* GetNode(size_t index) const;

    // Given a contiguous number of blocks after a starting block,
    // write out the bitmap to disk for the corresponding blocks.
    // Should only be called by PersistBlocks and FreeBlocks.
    void WriteBitmap(WriteTxn* txn, uint64_t nblocks, uint64_t start_block);

    // Given a node within the node map at an index, write it to disk.
    // Should only be called by AllocateNode and FreeNode.
    void WriteNode(WriteTxn* txn, size_t map_index);

    // Enqueues an update for allocated inode/block counts.
    void WriteInfo(WriteTxn* txn);

    // Creates an unique identifier for this instance. This is to be called only during
    // "construction".
    zx_status_t CreateFsId();

    // Verifies that the contents of a blob are valid.
    zx_status_t VerifyBlob(size_t node_index);

    // VnodeBlobs exist in the WAVLTree as long as one or more reference exists;
    // when the Vnode is deleted, it is immediately removed from the WAVL tree.
    using WAVLTreeByMerkle = fbl::WAVLTree<const uint8_t*,
                                           VnodeBlob*,
                                           MerkleRootTraits,
                                           VnodeBlob::TypeWavlTraits>;
    fbl::Mutex hash_lock_;
    WAVLTreeByMerkle open_hash_ __TA_GUARDED(hash_lock_){}; // All 'in use' blobs.
    WAVLTreeByMerkle closed_hash_ __TA_GUARDED(hash_lock_){}; // All 'closed' blobs.

    fbl::unique_fd blockfd_;
    block_info_t block_info_ = {};
    fifo_client_t* fifo_client_ = {};

    RawBitmap block_map_ = {};
    vmoid_t block_map_vmoid_ = {};
    fbl::unique_ptr<MappedVmo> node_map_ = {};
    vmoid_t node_map_vmoid_ = {};
    fbl::unique_ptr<MappedVmo> info_vmo_= {};
    vmoid_t info_vmoid_= {};

    // The reserved_blocks_ and reserved_nodes_ bitmaps only hold in-flight reservations.
    // At a steady state they will be empty.
    bitmap::RleBitmap reserved_blocks_ = {};
    bitmap::RleBitmap reserved_nodes_ = {};
    uint64_t fs_id_ = {};

    bool collecting_metrics_ = false;
    BlobfsMetrics metrics_ = {};

    fbl::Closure on_unmount_ = {};
};

typedef struct {
    bool readonly = false;
    bool metrics = false;
} blob_options_t;

zx_status_t blobfs_create(fbl::unique_ptr<Blobfs>* out, fbl::unique_fd blockfd);
zx_status_t blobfs_mount(async_t* async, fbl::unique_fd blockfd,
                         const blob_options_t* options, zx::channel root,
                         fbl::Closure on_unmount);
} // namespace blobfs
