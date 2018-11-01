// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trusty.h"

#include <assert.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <lib/async/default.h>
#include <zircon/status.h>

#include "controller.h"
#include "ring.h"

namespace trusty_virtio {

TrustyVirtioDevice::TrustyVirtioDevice(zx_device_t* bus_device, zx::bti bti,
                                       fbl::unique_ptr<Backend> backend)
    : Device(bus_device, fbl::move(bti), fbl::move(backend)) {
}

static zx_status_t trusty_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                                void* reply, size_t max, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

// Device bridge helpers
void trusty_unbind(void* ctx) {
    auto dev = static_cast<TrustyVirtioDevice*>(ctx);
    dev->Unbind();
}

void trusty_release(void* ctx) {
    auto dev = static_cast<TrustyVirtioDevice*>(ctx);
    dev->Release();
}

static zx_protocol_device_t kDeviceOps = {
    DEVICE_OPS_VERSION,
    nullptr, // get_protocol
    nullptr, // open
    nullptr, // openat
    nullptr, // close
    trusty_unbind,
    trusty_release,
    nullptr, // read
    nullptr, // write
    nullptr, // get_size
    trusty_ioctl,
    nullptr, // suspend
    nullptr, // resume
    nullptr, // rxrpc
    nullptr, // rxmsg
};

zx_status_t TrustyVirtioDevice::Init() {
    zx_status_t status = zx::channel::create(0, &server_channel_, &client_channel_);
    if (status) {
        TRACEF("failed to create channel pair: %s\n", zx_status_get_string(status));
        return status;
    }

    wait_.set_object(server_channel_.get());
    wait_.set_trigger(ZX_CHANNEL_READABLE);
    status = wait_.Begin(async_get_default_dispatcher());
    if (status) {
        TRACEF("failed to initialize Rx ring: %s\n", zx_status_get_string(status));
        return status;
    }

    status = rx_ring_.Init(kRxQueueId, kQueueSize);
    if (status != ZX_OK) {
        TRACEF("failed to initialize Rx ring: %s\n", zx_status_get_string(status));
        return status;
    }

    status = tx_ring_.Init(kTxQueueId, kQueueSize);
    if (status != ZX_OK) {
        TRACEF("failed to initialize Tx ring: %s\n", zx_status_get_string(status));
        return status;
    }

    auto cleanup = fbl::MakeAutoCall([this] {
        rx_buf_list_.clear();
    });

    auto& shm_pool = Controller::Instance()->shm_pool();
    for (uint16_t i = 0; i < kQueueSize; ++i) {
        fbl::unique_ptr<SharedMemory> shm;
        status = shm_pool.Allocate(kQueueElementSize, &shm);
        if (status != ZX_OK) {
            TRACEF("failed to allocate rx buffer\n");
            return ZX_ERR_NO_MEMORY;
        }

        uint16_t id;
        vring_desc* desc = rx_ring_.AllocDescChain(1, &id);
        if (!desc) {
            TRACEF("failed to allocate rx ring descriptor\n");
            return ZX_ERR_NO_MEMORY;
        }

        desc->addr = shm->paddr();
        desc->len = kQueueElementSize;
        desc->flags |= VRING_DESC_F_WRITE;

        rx_ring_.SubmitChain(id);
        rx_buf_list_.push_back(fbl::move(shm));
    }

    // Start the interrupt thread and set the driver OK status
    StartIrqThread();

    // Initialize the zx_device and publish us
    device_add_args_t args;
    memset(&args, 0, sizeof(args));
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-trusty";
    args.ctx = this;
    args.ops = &kDeviceOps;

    status = device_add(bus_device_, &args, &device_);
    if (status != ZX_OK) {
        TRACEF("Failed to add device: %s\n", zx_status_get_string(status));
        return status;
    }

    DriverStatusOk();
    DriverStatusAck();

    cleanup.cancel();
    return ZX_OK;
}

void TrustyVirtioDevice::OnMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        Stop();
        TRACEF("Failed to wait on message: %s\n", zx_status_get_string(status));
        return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {

        for (uint64_t i = 0; i < signal->count; i++) {
            auto& shm_pool = Controller::Instance()->shm_pool();

            fbl::unique_ptr<SharedMemory> message;
            status = shm_pool.Allocate(kQueueElementSize, &message);
            if (status != ZX_OK) {
                Stop();
                TRACEF("Failed to allocate shared memory: %s\n", zx_status_get_string(status));
                return;
            }

            uint16_t id;
            vring_desc* desc = tx_ring_.AllocDescChain(1, &id);
            if (!desc) {
                TRACEF("Run out of tx ring descriptor, should wait for a free one\n");
                break;
            }

            uint32_t actual_bytes;
            status = server_channel_.read(0, (void*)message->vaddr(), (uint32_t)message->size(),
                                          &actual_bytes, nullptr, 0, nullptr);
            if (status == ZX_ERR_SHOULD_WAIT) {
                break;
            }
            if (status != ZX_OK) {
                Stop();
                TRACEF("Failed to read channel: %s\n", zx_status_get_string(status));
                return;
            }

            desc->addr = message->paddr();
            desc->len = actual_bytes;

            tx_ring_.SubmitChain(id);
            tx_buf_list_.push_back(fbl::move(message));
        }

        tx_ring_.Kick();

        status = wait->Begin(async_get_default_dispatcher());
        if (status != ZX_OK) {
            TRACEF("Failed to wait on channel: %s\n", zx_status_get_string(status));
            Stop();
        }
        return;
    }

    // This will be observed after we drained all messages from the channel
    ZX_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    Stop();
}

void TrustyVirtioDevice::Release() {}
void TrustyVirtioDevice::Unbind() {}

void TrustyVirtioDevice::IrqRingUpdate() {

    auto free_tx_msg = [this](vring_used_elem* used_elem) {
        auto id = (uint16_t)used_elem->id;
        struct vring_desc* desc = tx_ring_.DescFromIndex(id);

        tx_buf_list_.erase_if([desc](const SharedMemory& shm) {
            return (desc->addr == shm.paddr());
        });
    };
    tx_ring_.IrqRingUpdate(free_tx_msg);

    auto receive_rx_msg = [this](vring_used_elem* used_elem) {
        auto id = (uint16_t)used_elem->id;
        struct vring_desc* desc = rx_ring_.DescFromIndex(id);

        auto shm = rx_buf_list_.find_if([desc](const SharedMemory& shm) {
            return (desc->addr == shm.paddr());
        });
        ZX_ASSERT(shm != rx_buf_list_.end());

        zx_status_t status = server_channel_.write(0, (void*)shm->vaddr(),
                                                   (uint32_t)shm->size(), nullptr, 0);
        ZX_ASSERT(status == ZX_OK);
    };
    rx_ring_.IrqRingUpdate(receive_rx_msg);
}

void TrustyVirtioDevice::IrqConfigChange() {}

} // namespace trusty_virtio
