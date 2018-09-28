// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <thread>
#include <vector>

#include <blobfs/fsck.h>
#include <fbl/auto_call.h>
#include <sys/stat.h>

#include "blobfs.h"

namespace {

// Add the blob described by |info| on host to the |blobfs| blobfs store.
zx_status_t AddBlob(blobfs::Blobfs* blobfs, MerkleInfo& info) {
    const char* path = info.path.c_str();
    fbl::unique_fd data_fd(open(path, O_RDONLY, 0644));
    if (!data_fd) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return ZX_ERR_IO;
    }
    zx_status_t status = blobfs::blobfs_add_blob_with_merkle(blobfs, data_fd.get(), info.length,
                                                             fbl::move(info.digest),
                                                             fbl::move(info.merkle));
    if (status != ZX_OK && status != ZX_ERR_ALREADY_EXISTS) {
        fprintf(stderr, "blobfs: Failed to add blob '%s': %d\n", path, status);
        return status;
    }
    return ZX_OK;
}

} // namespace

zx_status_t BlobfsCreator::Usage() {
    zx_status_t status = FsCreator::Usage();

    // Additional information about manifest format.
    fprintf(stderr, "\nEach manifest line must adhere to one of the following formats:\n");
    fprintf(stderr, "\t'dst/path=src/path'\n");
    fprintf(stderr, "\t'dst/path'\n");
    fprintf(stderr, "with one dst/src pair or single dst per line.\n");
    return status;
}

bool BlobfsCreator::IsCommandValid(Command command) {
    switch (command) {
    case Command::kMkfs:
    case Command::kFsck:
    case Command::kAdd:
        return true;
    default:
        return false;
    }
}

bool BlobfsCreator::IsOptionValid(Option option) {
    //TODO(planders): Add offset and length support to blobfs.
    switch (option) {
    case Option::kDepfile:
    case Option::kReadonly:
    case Option::kHelp:
        return true;
    default:
        return false;
    }
}

bool BlobfsCreator::IsArgumentValid(Argument argument) {
    switch (argument) {
    case Argument::kManifest:
    case Argument::kBlob:
        return true;
    default:
        return false;
    }
}

zx_status_t BlobfsCreator::ProcessManifestLine(FILE* manifest, const char* dir_path) {
    char src[PATH_MAX];
    src[0] = '\0';
    char dst[PATH_MAX];
    dst[0] = '\0';

    zx_status_t status;
    if ((status = ParseManifestLine(manifest, dir_path, &src[0], &dst[0])) != ZX_OK) {
        return status;
    }

    if (!strlen(src)) {
        fprintf(stderr, "Manifest line must specify source file\n");
        return ZX_ERR_INVALID_ARGS;
    }

    blob_list_.push_back(src);
    return ZX_OK;
}

zx_status_t BlobfsCreator::ProcessCustom(int argc, char** argv, uint8_t* processed) {
    constexpr uint8_t required_args = 2;
    if (strcmp(argv[0], "--blob")) {
        fprintf(stderr, "Argument not found: %s\n", argv[0]);
        return ZX_ERR_INVALID_ARGS;
    } else if (argc < required_args) {
        fprintf(stderr, "Not enough arguments for %s\n", argv[0]);
        return ZX_ERR_INVALID_ARGS;
    }

    blob_list_.push_back(argv[1]);
    *processed = required_args;
    return ZX_OK;
}

zx_status_t BlobfsCreator::CalculateRequiredSize(off_t* out) {
    std::vector<std::thread> threads;
    unsigned blob_index = 0;
    unsigned n_threads = std::thread::hardware_concurrency();
    if (!n_threads) {
        n_threads = 4;
    }
    zx_status_t status = ZX_OK;
    std::mutex mtx;
    for (unsigned j = n_threads; j > 0; j--) {
        threads.push_back(std::thread([&] {
            unsigned i = 0;
            while (true) {
                mtx.lock();
                if (status != ZX_OK) {
                    mtx.unlock();
                    return;
                }
                i = blob_index++;
                mtx.unlock();
                if (i >= blob_list_.size()) {
                    return;
                }
                const char* path = blob_list_[i].c_str();
                zx_status_t res;
                if ((res = AppendDepfile(path)) != ZX_OK) {
                    mtx.lock();
                    status = res;
                    mtx.unlock();
                    return;
                }

                MerkleInfo info;
                fbl::unique_fd data_fd(open(path, O_RDONLY, 0644));
                if ((res = blobfs::blobfs_create_merkle(data_fd.get(), &info.digest,
                                                        &info.merkle)) != ZX_OK) {
                    mtx.lock();
                    status = res;
                    mtx.unlock();
                    return;
                }

                struct stat s;
                if (fstat(data_fd.get(), &s) < 0) {
                    mtx.lock();
                    status = ZX_ERR_BAD_STATE;
                    mtx.unlock();
                    return;
                }
                info.path = path;
                info.length = s.st_size;

                mtx.lock();
                merkle_list_.push_back(fbl::move(info));
                mtx.unlock();
            }
        }));
    }

    for (unsigned i = 0; i < threads.size(); i++) {
        threads[i].join();
    }

    if (status != ZX_OK) {
        return status;
    }

    // Remove all duplicate blobs by first sorting the merkle trees by
    // digest, and then by reshuffling the vector to exclude duplicates.
    std::sort(merkle_list_.begin(), merkle_list_.end(), DigestCompare());
    auto compare = [](const MerkleInfo& lhs, const MerkleInfo& rhs) {
        return lhs.digest == rhs.digest;
    };
    auto it = std::unique(merkle_list_.begin(), merkle_list_.end(), compare);
    merkle_list_.resize(std::distance(merkle_list_.begin(), it));

    for (const auto& info : merkle_list_) {
        blobfs::blobfs_inode_t node;
        node.blob_size = info.length;
        data_blocks_ += MerkleTreeBlocks(node) + BlobDataBlocks(node);
    }

    blobfs::blobfs_info_t info;
    info.inode_count = blobfs::kBlobfsDefaultInodeCount;
    info.block_count = data_blocks_;
    *out = (data_blocks_ + blobfs::DataStartBlock(info)) * blobfs::kBlobfsBlockSize;
    return ZX_OK;
}

zx_status_t BlobfsCreator::Mkfs() {
    uint64_t block_count;
    if (blobfs::blobfs_get_blockcount(fd_.get(), &block_count)) {
        fprintf(stderr, "blobfs: cannot find end of underlying device\n");
        return ZX_ERR_IO;
    }

    int r = blobfs::blobfs_mkfs(fd_.get(), block_count);

    if (r >= 0 && !blob_list_.is_empty()) {
        zx_status_t status;
        if ((status = Add()) != ZX_OK) {
            return status;
        }
    }
    return r;
}

zx_status_t BlobfsCreator::Fsck() {
    zx_status_t status;
    fbl::unique_ptr<blobfs::Blobfs> vn;
    if ((status = blobfs::blobfs_create(&vn, fbl::move(fd_))) < 0) {
        return status;
    }

    return blobfs::blobfs_check(fbl::move(vn));
}

zx_status_t BlobfsCreator::Add() {
    if (blob_list_.is_empty()) {
        fprintf(stderr, "Adding a blob requires an additional file argument\n");
        return Usage();
    }

    zx_status_t status = ZX_OK;
    fbl::unique_ptr<blobfs::Blobfs> blobfs;
    if ((status = blobfs_create(&blobfs, fbl::move(fd_))) != ZX_OK) {
        return status;
    }

    std::vector<std::thread> threads;
    std::mutex mtx;

    unsigned n_threads = std::thread::hardware_concurrency();
    if (!n_threads) {
        n_threads = 4;
    }
    unsigned blob_index = 0;
    for (unsigned j = n_threads; j > 0; j--) {
        threads.push_back(std::thread([&] {
            unsigned i = 0;
            while (true) {
                mtx.lock();
                i = blob_index++;
                if (i >= merkle_list_.size()) {
                    mtx.unlock();
                    return;
                }
                mtx.unlock();

                zx_status_t res;
                if ((res = AddBlob(blobfs.get(), merkle_list_[i])) < 0) {
                    mtx.lock();
                    status = res;
                    mtx.unlock();
                    return;
                }
            }
        }));
    }

    for (unsigned i = 0; i < threads.size(); i++) {
        threads[i].join();
    }

    return status;
}

int main(int argc, char** argv) {
    BlobfsCreator blobfs;

    if (blobfs.ProcessAndRun(argc, argv) != ZX_OK) {
        return -1;
    }

    return 0;
}
