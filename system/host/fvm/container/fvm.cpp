// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <inttypes.h>
#include <sys/ioctl.h>

#include "fvm/container.h"

#if defined(__APPLE__)
#include <sys/disk.h>
#define IOCTL_GET_BLOCK_COUNT DKIOCGETBLOCKCOUNT
#endif

#if defined(__linux__)
#include <linux/fs.h>
#define IOCTL_GET_BLOCK_COUNT BLKGETSIZE
#endif

zx_status_t FvmContainer::Create(const char* path, size_t slice_size, off_t offset, off_t length,
                                 fbl::unique_ptr<FvmContainer>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<FvmContainer> fvmContainer(new (&ac) FvmContainer(path, slice_size, offset,
                                                                      length));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = fvmContainer->Init()) != ZX_OK) {
        return status;
    }

    *out = fbl::move(fvmContainer);
    return ZX_OK;
}

FvmContainer::FvmContainer(const char* path, size_t slice_size, off_t offset, off_t length)
    : Container(slice_size), valid_(false), disk_offset_(offset), disk_size_(length),
      vpart_hint_(1), pslice_hint_(1) {
    fd_.reset(open(path, O_RDWR, 0644));
    if (!fd_) {
        if (errno == ENOENT) {
            fd_.reset(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));

            if (!fd_) {
                fprintf(stderr, "Failed to create path %s\n", path);
                exit(-1);
            }

            xprintf("Created path %s\n", path);
        } else {
            fprintf(stderr, "Failed to open path %s: %s\n", path, strerror(errno));
            exit(-1);
        }
    }

    struct stat s;
    if (fstat(fd_.get(), &s) < 0) {
        fprintf(stderr, "Failed to stat %s\n", path);
        exit(-1);
    }

    uint64_t size = s.st_size;

    if (S_ISBLK(s.st_mode)) {
        uint64_t block_count;
        if (ioctl(fd_.get(), IOCTL_GET_BLOCK_COUNT, &block_count) >= 0) {
            size = block_count * 512;
        }
    }

    if (size < disk_offset_ + disk_size_) {
        fprintf(stderr, "Invalid file size %" PRIu64 " for specified offset+length\n", size);
        exit(-1);
    }

    // Even if disk size is 0, this will default to at least FVM_BLOCK_SIZE
    metadata_size_ = fvm::MetadataSize(disk_size_, slice_size_);

    fbl::AllocChecker ac;
    metadata_.reset(new (&ac) uint8_t[metadata_size_ * 2]);
    if (!ac.check()) {
        fprintf(stderr, "Unable to acquire resources for metadata\n");
        exit(-1);
    }

    // Clear entire primary copy of metadata
    memset(metadata_.get(), 0, metadata_size_);

    // If Container already exists, read metadata from disk.
    if (disk_size_ > 0) {
        if (lseek(fd_.get(), disk_offset_, SEEK_SET) < 0) {
            fprintf(stderr, "Seek reset failed\n");
            exit(-1);
        }

        ssize_t rd = read(fd_.get(), metadata_.get(), metadata_size_ * 2);
        if (rd != static_cast<ssize_t>(metadata_size_ * 2)) {
            fprintf(stderr, "Metadata read failed: expected %ld, actual %ld\n", metadata_size_, rd);
            exit(-1);
        }

        const void* backup = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(metadata_.get()) +
                                                     metadata_size_);

        // For now we always assume that primary metadata is primary
        if (fvm_validate_header(metadata_.get(), backup, metadata_size_, nullptr) == ZX_OK) {
            valid_ = true;

            if (memcmp(metadata_.get(), backup, metadata_size_)) {
                fprintf(stderr, "Warning: primary and backup metadata do not match\n");
            }
        }
    }
}

FvmContainer::~FvmContainer() = default;

zx_status_t FvmContainer::Init() {
    // Clear entire primary copy of metadata
    memset(metadata_.get(), 0, metadata_size_);

    // Superblock
    fvm::fvm_t* sb = SuperBlock();
    sb->magic = FVM_MAGIC;
    sb->version = FVM_VERSION;
    sb->pslice_count = (disk_size_ - metadata_size_ * 2) / slice_size_;
    sb->slice_size = slice_size_;
    sb->fvm_partition_size = disk_size_;
    sb->vpartition_table_size = fvm::kVPartTableLength;
    sb->allocation_table_size = fvm::AllocTableLength(disk_size_, slice_size_);
    sb->generation = 0;

    if (sb->pslice_count == 0) {
        return ZX_ERR_NO_SPACE;
    }

    dirty_ = true;
    valid_ = true;

    xprintf("fvm_init: Success\n");
    xprintf("fvm_init: Slice Count: %" PRIu64 ", size: %" PRIu64 "\n", sb->pslice_count, sb->slice_size);
    xprintf("fvm_init: Vpart offset: %zu, length: %zu\n",
           fvm::kVPartTableOffset, fvm::kVPartTableLength);
    xprintf("fvm_init: Atable offset: %zu, length: %zu\n",
           fvm::kAllocTableOffset, fvm::AllocTableLength(disk_size_, slice_size_));
    xprintf("fvm_init: Backup meta starts at: %zu\n",
           fvm::BackupStart(disk_size_, slice_size_));
    xprintf("fvm_init: Slices start at %zu, there are %zu of them\n",
           fvm::SlicesStart(disk_size_, slice_size_),
           fvm::UsableSlicesCount(disk_size_, slice_size_));
    return ZX_OK;
}

zx_status_t FvmContainer::Verify() const {
    CheckValid();
    const void* backup = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(metadata_.get()) +
                                                 metadata_size_);

    if (fvm_validate_header(metadata_.get(), backup, metadata_size_, nullptr) != ZX_OK) {
        fprintf(stderr, "Failed to validate header\n");
        return ZX_ERR_BAD_STATE;
    }

    fvm::fvm_t* sb = SuperBlock();

    xprintf("Total size is %zu\n", disk_size_);
    xprintf("Metadata size is %zu\n", metadata_size_);
    xprintf("Slice size is %" PRIu64 "\n", sb->slice_size);
    xprintf("Slice count is %" PRIu64 "\n", sb->pslice_count);

    off_t start = 0;
    off_t end = disk_offset_ + metadata_size_ * 2;
    size_t slice_index = 1;
    for (size_t vpart_index = 1; vpart_index < FVM_MAX_ENTRIES; ++vpart_index) {
        fvm::vpart_entry_t* vpart = nullptr;
        start = end;

        zx_status_t status;
        if ((status = GetPartition(vpart_index, &vpart)) != ZX_OK) {
            return status;
        }

        if (vpart->slices == 0) {
            break;
        }

        fbl::Vector<size_t> extent_lengths;
        size_t last_vslice = 0;

        for (; slice_index <= sb->pslice_count; ++slice_index) {
            fvm::slice_entry_t* slice = nullptr;
            if ((status = GetSlice(slice_index, &slice)) != ZX_OK) {
                return status;
            }

            if (slice->vpart != vpart_index) {
                break;
            }

            end += slice_size_;

            if (slice->vslice == last_vslice + 1) {
                extent_lengths[extent_lengths.size()-1] += slice_size_;
            } else {
                extent_lengths.push_back(slice_size_);
            }

            last_vslice = slice->vslice;
        }

        disk_format_t part;
        if ((status = Format::Detect(fd_.get(), start, &part)) != ZX_OK) {
            return status;
        }

        fbl::unique_fd dupfd(dup(fd_.get()));
        if (!dupfd) {
            fprintf(stderr, "Failed to duplicate fd\n");
            return ZX_ERR_INTERNAL;
        }

        if ((status = Format::Check(fbl::move(dupfd), start, end, extent_lengths, part)) != ZX_OK) {
            fprintf(stderr, "%s fsck returned an error.\n", vpart->name);
            return status;
        }

        xprintf("Found valid %s partition\n", vpart->name);
    }

    return ZX_OK;
}

zx_status_t FvmContainer::Commit() {
    if (!dirty_) {
        fprintf(stderr, "Commit: Nothing to write\n");
        return ZX_OK;
    }

    // If the FVM container has just been created, truncate it to an appropriate size
    if (disk_size_ == 0) {
        if (partitions_.is_empty()) {
            fprintf(stderr, "Cannot create new FVM container with 0 partitions\n");
            return ZX_ERR_INVALID_ARGS;
        }

        size_t required_size = 0;
        for (unsigned i = 0; i < partitions_.size(); i++) {
            required_size += partitions_[i].slice_count * slice_size_;
        }

        size_t total_size = required_size;
        size_t metadata_size = 0;

        while (total_size - (metadata_size * 2) < required_size || metadata_size < metadata_size_) {
            total_size = required_size + (metadata_size * 2);
            metadata_size = fvm::MetadataSize(total_size, slice_size_);
        }

        zx_status_t status;
        if ((status = GrowMetadata(metadata_size)) != ZX_OK) {
            return status;
        }

        if (ftruncate(fd_.get(), total_size) != 0) {
            fprintf(stderr, "Failed to truncate fvm container");
            return ZX_ERR_IO;
        }

        struct stat s;
        if (fstat(fd_.get(), &s) < 0) {
            fprintf(stderr, "Failed to stat container\n");
            return ZX_ERR_IO;
        }

        disk_size_ = s.st_size;

        if (disk_size_ != total_size) {
            fprintf(stderr, "Truncated to incorrect size\n");
            return ZX_ERR_IO;
        }

        fvm::fvm_t* sb = SuperBlock();
        sb->pslice_count = (disk_size_ - metadata_size_ * 2) / slice_size_;
        sb->fvm_partition_size = disk_size_;
        sb->allocation_table_size = fvm::AllocTableLength(disk_size_, slice_size_);
    }

    fvm_update_hash(metadata_.get(), metadata_size_);

    if (lseek(fd_.get(), disk_offset_, SEEK_SET) < 0) {
        fprintf(stderr, "Error seeking disk\n");
        return ZX_ERR_IO;
    }

    if (write(fd_.get(), metadata_.get(), metadata_size_) != static_cast<ssize_t>(metadata_size_)) {
        fprintf(stderr, "Error writing metadata to disk\n");
        return ZX_ERR_IO;
    }

    if (write(fd_.get(), metadata_.get(), metadata_size_) != static_cast<ssize_t>(metadata_size_)) {
        fprintf(stderr, "Error writing metadata to disk\n");
        return ZX_ERR_IO;
    }

    for (unsigned i = 0; i < partitions_.size(); i++) {
        zx_status_t status;
        if ((status = WritePartition(i)) != ZX_OK) {
            return status;
        }
    }

    xprintf("Successfully wrote FVM data to disk\n");
    return ZX_OK;
}

size_t FvmContainer::SliceSize() const {
    CheckValid();
    return slice_size_;
}

zx_status_t FvmContainer::AddPartition(const char* path, const char* type_name) {
    fbl::unique_ptr<Format> format;
    zx_status_t status;
    if ((status = Format::Create(path, type_name, &format)) != ZX_OK) {
        fprintf(stderr, "Failed to initialize partition\n");
        return status;
    }

    uint8_t guid[FVM_GUID_LEN];
    uint8_t type[FVM_GUID_LEN];
    char name[FVM_NAME_LEN];
    format->Guid(guid);
    format->Type(type);
    format->Name(name);
    uint32_t vpart_index;
    if ((status = AllocatePartition(type, guid, name, 1, &vpart_index)) != ZX_OK) {
        return status;
    }

    if ((status = format->MakeFvmReady(SliceSize(), vpart_index)) != ZX_OK) {
        return status;
    }

    uint32_t slice_count = 0;
    if ((status = format->GetSliceCount(&slice_count)) != ZX_OK) {
        return status;
    }

    // If allocated metadata is too small, grow it to an appropriate size
    size_t required_size = fvm::kAllocTableOffset + (pslice_hint_ + slice_count)
                           * sizeof(fvm::slice_entry_t);
    if ((status = GrowMetadata(required_size)) != ZX_OK) {
        return status;
    }

    // Allocate all slices for this partition
    uint32_t pslice_start = 0;
    uint32_t pslice_total = 0;
    unsigned extent_index = 0;
    while (true) {
        vslice_info_t vslice_info;
        zx_status_t status;
        if ((status = format->GetVsliceRange(extent_index, &vslice_info)) != ZX_OK) {
            if (status == ZX_ERR_OUT_OF_RANGE) {
                break;
            }
            return status;
        }

        uint32_t vslice = vslice_info.vslice_start / format->BlocksPerSlice();

        for (unsigned i = 0; i < vslice_info.slice_count; i++) {
            uint32_t pslice;

            if ((status = AllocateSlice(format->VpartIndex(), vslice + i, &pslice)) != ZX_OK) {
                return status;
            }

            if (!pslice_start) {
                pslice_start = pslice;
            }

            // On a new FVM container, pslice allocation is expected to be contiguous.
            if (pslice != pslice_start + pslice_total) {
                fprintf(stderr, "Unexpected error during slice allocation\n");
                return ZX_ERR_INTERNAL;
            }

            pslice_total++;
        }

        extent_index++;
    }

    partition_info_t partition;
    partition.format = fbl::move(format);
    partition.vpart_index = vpart_index;
    partition.pslice_start = pslice_start;
    partition.slice_count = slice_count;
    partitions_.push_back(fbl::move(partition));
    return ZX_OK;
}

void FvmContainer::CheckValid() const {
    if (!valid_) {
        fprintf(stderr, "Error: FVM is invalid\n");
        exit(-1);
    }
}

zx_status_t FvmContainer::GrowMetadata(size_t new_size) {
    if (new_size <= metadata_size_) {
        return ZX_OK;
    } else if (disk_size_ > 0) {
        fprintf(stderr, "Cannot grow metadata for disk with established size\n");
        return ZX_ERR_ACCESS_DENIED;
    }

    xprintf("Growing metadata from %zu to %zu\n", metadata_size_, new_size);
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> new_metadata(new (&ac) uint8_t[new_size * 2]);
    if (!ac.check()) {
        fprintf(stderr, "Unable to acquire resources for new metadata\n");
        return ZX_ERR_NO_MEMORY;
    }

    memcpy(new_metadata.get(), metadata_.get(), metadata_size_);
    memset(new_metadata.get() + metadata_size_, 0, new_size - metadata_size_);

    metadata_ = fbl::move(new_metadata);
    metadata_size_ = new_size;
    return ZX_OK;
}

zx_status_t FvmContainer::AllocatePartition(uint8_t* type, uint8_t* guid, const char* name,
                                            uint32_t slices, uint32_t* vpart_index) {
    CheckValid();
    for (unsigned index = vpart_hint_; index < FVM_MAX_ENTRIES; index++) {
        zx_status_t status;
        fvm::vpart_entry_t* vpart = nullptr;
        if ((status = GetPartition(index, &vpart)) != ZX_OK) {
            fprintf(stderr, "Failed to retrieve partition %u\n", index);
            return status;
        }

        // Make sure this vpartition has not already been allocated
        if (vpart->slices == 0) {
            vpart->init(type, guid, slices, name, 0);
            vpart_hint_ = index + 1;
            dirty_ = true;
            *vpart_index = index;
            return ZX_OK;
        }
    }

    fprintf(stderr, "Unable to find any free partitions\n");
    return ZX_ERR_INTERNAL;
}

zx_status_t FvmContainer::AllocateSlice(uint32_t vpart, uint32_t vslice, uint32_t* pslice) {
    CheckValid();
    fvm::fvm_t* sb = SuperBlock();

    for (uint32_t index = pslice_hint_; index < sb->pslice_count; index++) {
        zx_status_t status;
        fvm::slice_entry_t* slice = nullptr;
        if ((status = GetSlice(index, &slice)) != ZX_OK) {
            fprintf(stderr, "Failed to retrieve slice %u\n", index);
            return status;
        }

        if (slice->vpart != FVM_SLICE_FREE) {
            continue;
        }

        pslice_hint_ = index + 1;

        slice->vpart = vpart & VPART_MAX;
        slice->vslice = vslice & VSLICE_MAX;
        dirty_ = true;
        *pslice = index;
        return ZX_OK;
    }

    fprintf(stderr, "Unable to find any free slices\n");
    return ZX_ERR_INTERNAL;
}

zx_status_t FvmContainer::GetPartition(size_t index, fvm::vpart_entry_t** out) const {
    CheckValid();

    if (index < 1 || index > FVM_MAX_ENTRIES) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    uintptr_t metadata_start = reinterpret_cast<uintptr_t>(metadata_.get());
    uintptr_t offset = static_cast<uintptr_t>(fvm::kVPartTableOffset +
                                                  index * sizeof(fvm::vpart_entry_t));
    *out = reinterpret_cast<fvm::vpart_entry_t*>(metadata_start + offset);
    return ZX_OK;
}

zx_status_t FvmContainer::GetSlice(size_t index, fvm::slice_entry_t** out) const {
    CheckValid();

    if (index < 1 || index > SuperBlock()->pslice_count) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    uintptr_t metadata_start = reinterpret_cast<uintptr_t>(metadata_.get());
    uintptr_t offset = static_cast<uintptr_t>(fvm::kAllocTableOffset +
                                              index * sizeof(fvm::slice_entry_t));
    *out = reinterpret_cast<fvm::slice_entry_t*>(metadata_start + offset);
    return ZX_OK;
}

zx_status_t FvmContainer::WritePartition(unsigned part_index) {
    CheckValid();
    if (part_index > partitions_.size()) {
        fprintf(stderr, "Error: Tried to access partition %u / %zu\n",
                part_index, partitions_.size());
        return ZX_ERR_OUT_OF_RANGE;
    }

    unsigned extent_index = 0;
    partition_info_t* partition = &partitions_[part_index];
    Format* format = partition->format.get();
    uint32_t pslice_start = partition->pslice_start;

    while (true) {
        zx_status_t status;
        if ((status = WriteExtent(extent_index++, format, &pslice_start)) != ZX_OK) {
            if (status != ZX_ERR_OUT_OF_RANGE) {
                return status;
            }

            return ZX_OK;
        }
    }
}

zx_status_t FvmContainer::WriteExtent(unsigned extent_index, Format* format, uint32_t* pslice) {
    vslice_info_t vslice_info;
    zx_status_t status;
    if ((status = format->GetVsliceRange(extent_index, &vslice_info)) != ZX_OK) {
        return status;
    }

    // Write each slice in the given extent
    uint32_t current_block = 0;
    for (unsigned i = 0; i < vslice_info.slice_count; i++) {
        // Write each block in this slice
        for (uint32_t j = 0; j < format->BlocksPerSlice(); j++) {
            // If we have gone beyond the blocks written to partition file, write empty block
            if (current_block >= vslice_info.block_count) {
                if (!vslice_info.zero_fill) {
                    break;
                }

                format->EmptyBlock();
            } else {
                if ((status = format->FillBlock(vslice_info.block_offset + current_block)) != ZX_OK) {
                    fprintf(stderr, "Failed to read block from minfs\n");
                    return status;
                }

                current_block++;
            }

            if ((status = WriteData(*pslice, j, format->BlockSize(), format->Data())) != ZX_OK) {
                fprintf(stderr, "Failed to write data to FVM\n");
                return status;
            }
        }
        (*pslice)++;
    }

    return ZX_OK;
}

zx_status_t FvmContainer::WriteData(uint32_t pslice, uint32_t block_offset, size_t block_size,
                                    void* data) {
    CheckValid();

    if (block_offset * block_size > slice_size_) {
        fprintf(stderr, "Not enough space in slice\n");
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (lseek(fd_.get(), disk_offset_ + fvm::SliceStart(disk_size_, slice_size_, pslice) +
                block_offset * block_size, SEEK_SET) < 0) {
        return ZX_ERR_BAD_STATE;
    }

    ssize_t r = write(fd_.get(), data, block_size);
    if (r != block_size) {
        fprintf(stderr, "Failed to write data to FVM\n");
        return ZX_ERR_BAD_STATE;
    }

    return ZX_OK;
}

fvm::fvm_t* FvmContainer::SuperBlock() const {
    return static_cast<fvm::fvm_t*>((void*)metadata_.get());
}
