// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <lib/user_copy/user_ptr.h>
#include <object/buffer_chain.h>
#include <object/handle.h>
#include <zircon/types.h>

constexpr uint32_t kMaxMessageSize = 65536u;
constexpr uint32_t kMaxMessageHandles = 64u;

// ensure public constants are aligned
static_assert(ZX_CHANNEL_MAX_MSG_BYTES == kMaxMessageSize, "");
static_assert(ZX_CHANNEL_MAX_MSG_HANDLES == kMaxMessageHandles, "");

class Handle;

class MessagePacket final : public fbl::DoublyLinkedListable<fbl::unique_ptr<MessagePacket>>,
                            fbl::Recyclable<MessagePacket> {
public:
    // Creates a message packet containing the provided data and space for
    // |num_handles| handles. The handles array is uninitialized and must
    // be completely overwritten by clients.
    static zx_status_t Create(user_in_ptr<const void> data, uint32_t data_size,
                              uint32_t num_handles,
                              fbl::unique_ptr<MessagePacket>* msg);
    static zx_status_t Create(const void* data, uint32_t data_size,
                              uint32_t num_handles,
                              fbl::unique_ptr<MessagePacket>* msg);

    uint32_t data_size() const { return data_size_; }

    // Copies the packet's |data_size()| bytes to |buf|.
    // Returns an error if |buf| points to a bad user address.
    zx_status_t CopyDataTo(user_out_ptr<void> buf) const {
        return buffer_chain_->CopyOut(buf, payload_offset_, data_size_);
    }

    uint32_t num_handles() const { return num_handles_; }
    Handle* const* handles() const { return handles_; }
    Handle** mutable_handles() { return handles_; }

    void set_owns_handles(bool own_handles) { owns_handles_ = own_handles; }

    // zx_channel_call treats the leading bytes of the payload as
    // a transaction id of type zx_txid_t.
    zx_txid_t get_txid() const {
        if (data_size_ < sizeof(zx_txid_t)) {
            return 0;
        }
        // The first few bytes of the payload are a zx_txid_t.
        void* payload_start = buffer_chain_->buffers()->front().data() + payload_offset_;
        return *reinterpret_cast<zx_txid_t*>(payload_start);
    }

    void set_txid(zx_txid_t txid) {
        if (data_size_ >= sizeof(zx_txid_t)) {
            void* payload_start = buffer_chain_->buffers()->front().data() + payload_offset_;
            *(reinterpret_cast<zx_txid_t*>(payload_start)) = txid;
        }
    }

private:
    MessagePacket(BufferChain* chain, uint32_t data_size, uint32_t payload_offset,
                  uint16_t num_handles, Handle** handles)
        : buffer_chain_(chain), handles_(handles), data_size_(data_size),
          payload_offset_(payload_offset), num_handles_(num_handles), owns_handles_(false) {}

    friend class fbl::unique_ptr<MessagePacket>;
    ~MessagePacket() {
        DEBUG_ASSERT(!InContainer());
        if (owns_handles_) {
            for (size_t ix = 0; ix != num_handles_; ++ix) {
                // Delete the handle via HandleOwner dtor.
                HandleOwner ho(handles_[ix]);
            }
        }
    }

    friend class fbl::Recyclable<MessagePacket>;
    void fbl_recycle();

    static zx_status_t CreateCommon(uint32_t data_size, uint32_t num_handles,
                                    fbl::unique_ptr<MessagePacket>* msg);

    BufferChain* buffer_chain_;
    Handle** const handles_;
    const uint32_t data_size_;
    const uint32_t payload_offset_;
    const uint16_t num_handles_;
    bool owns_handles_;
};
