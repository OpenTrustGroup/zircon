// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/nand.h>
#include <ddktl/device.h>
#include <ddktl/protocol/bad-block.h>
#include <ddktl/protocol/nand.h>

#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <zircon/types.h>

#include "bad-block.h"

namespace nand {

class NandPartDevice;
using DeviceType = ddk::Device<NandPartDevice, ddk::GetSizable, ddk::GetProtocolable,
                               ddk::Unbindable>;

class NandPartDevice : public DeviceType,
                       public ddk::NandProtocol<NandPartDevice>,
                       public ddk::BadBlockable<NandPartDevice> {
public:
    // Spawns device nodes based on parent node.
    static zx_status_t Create(zx_device_t* parent);

    zx_status_t Bind(const char* name, uint32_t copy_count);

    // Device protocol implementation.
    zx_off_t DdkGetSize() {
        //TODO: use query() results, *but* fvm returns different query and getsize
        // results, and the latter are dynamic...
        return device_get_size(parent());
    }
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* protocol);
    void DdkUnbind() { DdkRemove(); }
    void DdkRelease() { delete this; }

    // nand protocol implementation.
    void Query(nand_info_t* info_out, size_t* nand_op_size_out);
    void Queue(nand_op_t* op);
    zx_status_t GetFactoryBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
                                       uint32_t* num_bad_blocks);

    // Bad block protocol implementation.
    zx_status_t GetBadBlockList(uint32_t* bad_block_list, uint32_t bad_block_list_len,
                                uint32_t* bad_block_count);
    zx_status_t MarkBlockBad(uint32_t block);

private:
    explicit NandPartDevice(zx_device_t* parent, const nand_protocol_t& nand_proto,
                            fbl::RefPtr<BadBlock> bad_block, size_t parent_op_size,
                            const nand_info_t& nand_info, uint32_t erase_block_start)
        : DeviceType(parent), nand_proto_(nand_proto), nand_(&nand_proto_),
          parent_op_size_(parent_op_size), nand_info_(nand_info),
          erase_block_start_(erase_block_start), bad_block_(fbl::move(bad_block)) {}

    DISALLOW_COPY_ASSIGN_AND_MOVE(NandPartDevice);

    nand_protocol_t nand_proto_;
    ddk::NandProtocolProxy nand_;

    // op_size for parent device.
    size_t parent_op_size_;
    // info about nand.
    nand_info_t nand_info_;
    // First erase block for the partition.
    uint32_t erase_block_start_;
    // Device specific bad block info. Shared between all devices for a given
    // parent device.
    fbl::RefPtr<BadBlock> bad_block_;
    // Cached list of bad blocks for this partition. Lazily instantiated.
    fbl::Array<uint32_t> bad_block_list_;
};

} // namespace nand
