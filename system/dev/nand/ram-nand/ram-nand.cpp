// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ram-nand.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <zircon/assert.h>
#include <zircon/device/ram-nand.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace {

struct RamNandOp {
    nand_op_t op;
    list_node_t node;
};

}  // namespace

NandDevice::NandDevice(const NandParams& params) : params_(params) {
}

NandDevice::~NandDevice() {
    if (thread_created_) {
        Kill();
        completion_signal(&wake_signal_);
        int result_code;
        thrd_join(worker_, &result_code);

        for (;;) {
            RamNandOp* nand_op = list_remove_head_type(&txn_list_, RamNandOp, node);
            if (!nand_op) {
                break;
            }
            nand_op_t* operation = &nand_op->op;
            operation->completion_cb(operation, ZX_ERR_BAD_STATE);
        }
    }

    if (mapped_addr_) {
        zx_vmar_unmap(zx_vmar_root_self(), mapped_addr_, GetSize());
    }
}

zx_status_t NandDevice::Init(char name[NAME_MAX]) {
    static uint64_t dev_count = 0;
    snprintf(name, NAME_MAX, "ram-nand-%" PRIu64, dev_count++);

    zx_status_t status = zx::vmo::create(GetSize(), 0, &vmo_);
    if (status != ZX_OK) {
        return status;
    }

    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_.get(), 0, GetSize(),
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                         &mapped_addr_);
    if (status != ZX_OK) {
        return status;
    }
    memset(reinterpret_cast<char*>(mapped_addr_), 0xff, GetSize());

    list_initialize(&txn_list_);
    if (thrd_create(&worker_, WorkerThreadStub, this) != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    }
    thread_created_ = true;

    return ZX_OK;
}

void NandDevice::Unbind() {
    Kill();
    completion_signal(&wake_signal_);
}

zx_status_t NandDevice::Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                              void* out_buf, size_t out_len, size_t* out_actual) {
    {
        fbl::AutoLock lock(&lock_);
        if (dead_) {
            return ZX_ERR_BAD_STATE;
        }
    }

    switch (op) {
    case IOCTL_RAM_NAND_UNLINK:
        Unbind();
        return ZX_OK;

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void NandDevice::Query(nand_info_t* info_out, size_t* nand_op_size_out) {
    *info_out = params_;
    *nand_op_size_out = sizeof(RamNandOp);
}

void NandDevice::Queue(nand_op_t* operation) {
    uint32_t max_pages = params_.NumPages();
    switch (operation->command) {
    case NAND_OP_READ:
    case NAND_OP_WRITE: {
        if (operation->rw.offset_nand >= max_pages || !operation->rw.length ||
            (max_pages - operation->rw.offset_nand) < operation->rw.length) {
            operation->completion_cb(operation, ZX_ERR_OUT_OF_RANGE);
            return;
        }
        if (operation->rw.data_vmo == ZX_HANDLE_INVALID &&
            operation->rw.oob_vmo == ZX_HANDLE_INVALID) {
            operation->completion_cb(operation, ZX_ERR_BAD_HANDLE);
            return;
        }
        break;
    }
    case NAND_OP_ERASE:
        if (!operation->erase.num_blocks ||
            operation->erase.first_block >= params_.num_blocks ||
            params_.num_blocks - operation->erase.first_block < operation->erase.num_blocks) {
            operation->completion_cb(operation, ZX_ERR_OUT_OF_RANGE);
            return;
        }
        break;

    default:
        operation->completion_cb(operation, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    if (AddToList(operation)) {
        completion_signal(&wake_signal_);
    } else {
        operation->completion_cb(operation, ZX_ERR_BAD_STATE);
    }
}

void NandDevice::GetBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
                                 uint32_t* num_bad_blocks) {
    *num_bad_blocks = 0;
}

void NandDevice::Kill() {
    fbl::AutoLock lock(&lock_);
    dead_ = true;
}

bool NandDevice::AddToList(nand_op_t* operation) {
    fbl::AutoLock lock(&lock_);
    bool is_dead = dead_;
    if (!dead_) {
        RamNandOp* nand_op = reinterpret_cast<RamNandOp*>(operation);
        list_add_tail(&txn_list_, &nand_op->node);
    }
    return !is_dead;
}

bool NandDevice::RemoveFromList(nand_op_t** operation) {
    fbl::AutoLock lock(&lock_);
    bool is_dead = dead_;
    if (!dead_) {
        RamNandOp* nand_op = list_remove_head_type(&txn_list_, RamNandOp, node);
        *operation = reinterpret_cast<nand_op_t*>(nand_op);
    }
    return !is_dead;
}

int NandDevice::WorkerThread() {
    for (;;) {
        nand_op_t* operation;
        for (;;) {
            if (!RemoveFromList(&operation)) {
                return 0;
            }
            if (operation) {
                completion_reset(&wake_signal_);
                break;
            } else {
                completion_wait(&wake_signal_, ZX_TIME_INFINITE);
            }
        }

        zx_status_t status = ZX_OK;

        switch (operation->command) {
        case NAND_OP_READ:
        case NAND_OP_WRITE:
            status = ReadWriteData(operation);
            if (status == ZX_OK) {
                status = ReadWriteOob(operation);
            }
            break;

        case NAND_OP_ERASE: {
            status = Erase(operation);
            break;
        }
        default:
            ZX_DEBUG_ASSERT(false);  // Unexpected.
        }

        operation->completion_cb(operation, status);
    }
}

int NandDevice::WorkerThreadStub(void* arg) {
    NandDevice* device = static_cast<NandDevice*>(arg);
    return device->WorkerThread();
}

zx_status_t NandDevice::ReadWriteData(nand_op_t* operation) {
    if (operation->rw.data_vmo == ZX_HANDLE_INVALID) {
        return ZX_OK;
    }

    uint32_t nand_addr = operation->rw.offset_nand * params_.page_size;
    uint64_t vmo_addr = operation->rw.offset_data_vmo * params_.page_size;
    uint32_t length = operation->rw.length * params_.page_size;
    void* addr = reinterpret_cast<char*>(mapped_addr_) + nand_addr;

    if (operation->command == NAND_OP_READ) {
        operation->rw.corrected_bit_flips = 0;
        return zx_vmo_write(operation->rw.data_vmo, addr, vmo_addr, length);
    }

    ZX_DEBUG_ASSERT(operation->command == NAND_OP_WRITE);

    // Likely something bad is going on if writing multiple blocks.
    ZX_DEBUG_ASSERT_MSG(operation->rw.length <= params_.pages_per_block,
                        "Writing multiple blocks");
    ZX_DEBUG_ASSERT_MSG(operation->rw.offset_nand / params_.pages_per_block ==
                        (operation->rw.offset_nand + operation->rw.length - 1)
                        / params_.pages_per_block,
                        "Writing multiple blocks");

    return zx_vmo_read(operation->rw.data_vmo, addr, vmo_addr, length);
}

zx_status_t NandDevice::ReadWriteOob(nand_op_t* operation) {
    if (operation->rw.oob_vmo == ZX_HANDLE_INVALID) {
        return ZX_OK;
    }

    uint32_t nand_addr = MainDataSize() + operation->rw.offset_nand * params_.oob_size;
    uint64_t vmo_addr = operation->rw.offset_oob_vmo * params_.page_size;
    uint32_t length = operation->rw.length * params_.oob_size;
    void* addr = reinterpret_cast<char*>(mapped_addr_) + nand_addr;

    if (operation->command == NAND_OP_READ) {
        operation->rw.corrected_bit_flips = 0;
        return zx_vmo_write(operation->rw.oob_vmo, addr, vmo_addr, length);
    }

    ZX_DEBUG_ASSERT(operation->command == NAND_OP_WRITE);
    return zx_vmo_read(operation->rw.oob_vmo, addr, vmo_addr, length);
}

zx_status_t NandDevice::Erase(nand_op_t* operation) {
    ZX_DEBUG_ASSERT(operation->command == NAND_OP_ERASE);

    uint32_t block_size = params_.page_size * params_.pages_per_block;
    uint32_t nand_addr = operation->erase.first_block * block_size;
    uint32_t length = operation->erase.num_blocks * block_size;
    void* addr = reinterpret_cast<char*>(mapped_addr_) + nand_addr;

    memset(addr, 0xff, length);

    // Clear the OOB area:
    uint32_t oob_per_block = params_.oob_size * params_.pages_per_block;
    length = operation->erase.num_blocks * oob_per_block;
    nand_addr = MainDataSize() + operation->erase.first_block * oob_per_block;
    addr = reinterpret_cast<char*>(mapped_addr_) + nand_addr;

    memset(addr, 0xff, length);

    return ZX_OK;
}
