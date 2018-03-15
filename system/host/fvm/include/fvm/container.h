// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lz4/lz4frame.h>

#include <fbl/vector.h>
#include <fbl/unique_fd.h>
#include <fvm/fvm-sparse.h>

#include "format.h"

typedef enum {
    NONE,
    LZ4,
} compress_type_t;

// A Container represents a method of storing multiple file system partitions in an
// FVM-recognizable format
class Container {
public:
    // Returns a Container representation of the FVM within the given |path|, starting at |offset|
    // bytes of length |length| bytes. Will return an error if the file does not exist or is not a
    // valid Container type.
    static zx_status_t Create(const char* path, off_t offset, off_t length,
                              fbl::unique_ptr<Container>* out);

    Container(size_t slice_size)
        : dirty_(false), slice_size_(slice_size) {}
    virtual ~Container() {}

    // Resets the Container state so we are ready to add a new set of partitions
    // Init must be called separately from the constructor, as it will overwrite data pertinent to
    // an existing Container.
    virtual zx_status_t Init() = 0;

    // Reports various information about the Container, e.g. number of partitions, and runs fsck on
    // all supported partitions (blobfs, minfs)
    virtual zx_status_t Verify() const = 0;

    // Commits the Container data to disk
    virtual zx_status_t Commit() = 0;

    // Returns the Container's specified slice size (in bytes)
    virtual size_t SliceSize() const = 0;

    // Given a path to a valid file system partition, adds that partition to the container
    virtual zx_status_t AddPartition(const char* path, const char* type_name) = 0;

protected:
    fbl::unique_fd fd_;
    bool dirty_;
    size_t slice_size_;
};

class FvmContainer final : public Container {
    typedef struct {
        uint32_t vpart_index;
        uint32_t pslice_start;
        uint32_t slice_count;
        fbl::unique_ptr<Format> format;
    } partition_info_t;

public:
    // Creates an FVM container at the given path, creating a new file if one does not already
    // exist. |offset| and |length| are provided to specify the offset (in bytes) and the length
    // (in bytes) of the FVM within the file. For a file that has not yet been created, these
    // should both be 0. For a file that exists, if not otherwise specified the offset should be 0
    // and the length should be the size of the file.
    static zx_status_t Create(const char* path, size_t slice_size, off_t offset, off_t length,
                              fbl::unique_ptr<FvmContainer>* out);
    FvmContainer(const char* path, size_t slice_size, off_t offset, off_t length);
    ~FvmContainer();
    zx_status_t Init() final;
    zx_status_t Verify() const final;
    zx_status_t Commit() final;
    size_t SliceSize() const final;
    zx_status_t AddPartition(const char* path, const char* type_name) final;

private:
    bool valid_;
    size_t metadata_size_;
    size_t disk_offset_;
    size_t disk_size_;
    uint32_t vpart_hint_;
    uint32_t pslice_hint_;
    fbl::unique_ptr<uint8_t[]> metadata_;
    fbl::Vector<partition_info_t> partitions_;

    void CheckValid() const;

    // Grow in-memory metadata representation to the specified size
    zx_status_t GrowMetadata(size_t metadata_size);

    // Allocate new partition (in memory)
    zx_status_t AllocatePartition(uint8_t* type, uint8_t* guid, const char* name, uint32_t slices,
                                  uint32_t* vpart_index);
    // Allocate new slice for given partition (in memory)
    zx_status_t AllocateSlice(uint32_t vpart, uint32_t vslice, uint32_t* pslice);

    // Helpers to grab reference to partition/slice from metadata
    zx_status_t GetPartition(size_t index, fvm::vpart_entry_t** out) const;
    zx_status_t GetSlice(size_t index, fvm::slice_entry_t** out) const;

    // Write the |part_index|th partition to disk
    zx_status_t WritePartition(unsigned part_index);
    // Write a partition's |extent_index|th extent to disk. |*pslice| is the starting pslice, and
    // is updated to reflect the latest written pslice.
    zx_status_t WriteExtent(unsigned extent_index, Format* format, uint32_t* pslice);
    // Write one data block of size |block_size| to disk at |block_offset| within pslice |pslice|
    zx_status_t WriteData(uint32_t pslice, uint32_t block_offset, size_t block_size, void* data);
    fvm::fvm_t* SuperBlock() const;
};

class SparseContainer final : public Container {
    typedef struct {
        fvm::partition_descriptor_t descriptor;
        fbl::Vector<fvm::extent_descriptor_t> extents;
        fbl::unique_ptr<Format> format;
    } partition_info_t;

public:
    static zx_status_t Create(const char* path, size_t slice_size, compress_type_t compress,
                              fbl::unique_ptr<SparseContainer>* out);
    SparseContainer(const char* path, uint64_t slice_size, compress_type_t compress);
    ~SparseContainer();
    zx_status_t Init() final;
    zx_status_t Verify() const final;
    zx_status_t Commit() final;
    size_t SliceSize() const final;
    zx_status_t AddPartition(const char* path, const char* type_name) final;

private:
    bool valid_;
    compress_type_t compress_;
    size_t disk_size_;
    size_t extent_size_;
    fvm::sparse_image_t image_;
    fbl::Vector<partition_info_t> partitions_;

    zx_status_t AllocatePartition(fbl::unique_ptr<Format> format);
    zx_status_t AllocateExtent(uint32_t part_index, uint64_t slice_start, uint64_t slice_count,
                               uint64_t extent_length);

    typedef struct {
        LZ4F_compressionContext_t cctx;
        size_t data_size = 0;
        size_t offset = 0;
        fbl::unique_ptr<uint8_t[]> data;

        size_t size() {
            return data_size;
        }

        void* buf() {
            return data.get() + offset;
        }

        bool reset(size_t size) {
            data_size = size;
            fbl::AllocChecker ac;
            data.reset(new (&ac) uint8_t[size]);
            return ac.check();
        }
    } compression_t;

    zx_status_t SetupCompression(compression_t* comp, size_t max_len);
    zx_status_t WriteData(const void* data, size_t length, compression_t* comp);
    zx_status_t FinishCompression(compression_t* comp);
};
