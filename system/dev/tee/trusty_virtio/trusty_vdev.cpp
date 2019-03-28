// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trusty_vdev.h"

#include <assert.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <zircon/device/trusty-vdev.h>
#include <zircon/status.h>

#include "controller.h"
#include "ring.h"

namespace trusty_virtio {

TrustyVirtioDevice::TrustyVirtioDevice(zx_device_t* bus_device, zx::bti bti,
                                       fbl::unique_ptr<virtio::Backend> backend)
    : DeviceType(bus_device), virtio::Device(bus_device, fbl::move(bti), fbl::move(backend)),
      loop_(&kAsyncLoopConfigNoAttachToThread) {}

TrustyVirtioDevice::~TrustyVirtioDevice() {
    Stop();
}

zx_status_t TrustyVirtioDevice::DdkClose(uint32_t flags) {
    // TODO(sy): handle this
    return ZX_OK;
}

void TrustyVirtioDevice::DdkRelease() {
    Release();

    // devmgr has given up ownership, so we must clean ourself up.
    delete this;
}

zx_status_t TrustyVirtioDevice::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                         void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_TRUSTY_VDEV_START: {
        if ((in_len != 0) || (out_len < sizeof(zx_handle_t))) {
            return ZX_ERR_INVALID_ARGS;
        }

        if (msg_channel() != ZX_HANDLE_INVALID) {
            return ZX_ERR_BAD_STATE;
        }

        zx::channel ch0, ch1;
        zx_status_t status = zx::channel::create(0, &ch0, &ch1);
        if (status != ZX_OK) {
            zxlogf(ERROR, "Failed to create channel pair: %s",
                   zx_status_get_string(status));
            return status;
        }

        status = Start(fbl::move(ch1));
        if (status != ZX_OK) {
            zxlogf(ERROR, "Failed to start trusty vdev: %s",
                   zx_status_get_string(status));
            return status;
        }

        zx_handle_t out = ch0.release();
        memcpy(out_buf, &out, sizeof(zx_handle_t));
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }

    case IOCTL_TRUSTY_VDEV_GET_MESSAGE_SIZE: {
        if ((in_len != 0) || (out_len < sizeof(size_t))) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(out_buf, &kQueueElementSize, sizeof(size_t));
        *out_actual = sizeof(size_t);
        return ZX_OK;
    }

    case IOCTL_TRUSTY_VDEV_GET_SHM_RESOURCE: {
        if ((in_len != 0) || (out_len < sizeof(zx_handle_t))) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t h;
        const char name[] = "shm_rsc";
        zx_status_t status = zx_resource_create(get_root_resource(),
                                                ZX_RSRC_KIND_NSMEM,
                                                0, 0, name, sizeof(name), &h);
        if (status < 0) {
            return status;
        }
        memcpy(out_buf, &h, sizeof(zx_handle_t));
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }

    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t TrustyVirtioDevice::Init() {
    zx_status_t status = loop_.StartThread("trusty_vdev", &loop_thread_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to start loop %d\n", status);
        return status;
    }

    status = rx_ring_.Init(kRxQueueId, kQueueSize);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to initialize Rx ring: %s\n", zx_status_get_string(status));
        return status;
    }

    status = tx_ring_.Init(kTxQueueId, kQueueSize);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to initialize Tx ring: %s\n", zx_status_get_string(status));
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
            zxlogf(ERROR, "Failed to allocate rx buffer\n");
            return ZX_ERR_NO_MEMORY;
        }

        uint16_t id;
        vring_desc* desc = rx_ring_.AllocDescChain(1, &id);
        if (!desc) {
            zxlogf(ERROR, "Failed to allocate rx ring descriptor\n");
            return ZX_ERR_NO_MEMORY;
        }

        desc->addr = shm->paddr();
        desc->len = kQueueElementSize;
        desc->flags |= VRING_DESC_F_WRITE;

        rx_ring_.SubmitChain(id);
        rx_buf_list_.push_back(fbl::move(shm));
    }

    rx_ring_.Kick();

    // Start the interrupt thread and set the driver OK status
    StartIrqThread();

    status = DdkAdd("virtio-trusty");
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to add device, status=%s\n",
               zx_status_get_string(status));
        return status;
    }

    DriverStatusOk();
    DriverStatusAck();

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t TrustyVirtioDevice::Start(zx::channel msg_channel) {
    msg_channel_ = fbl::move(msg_channel);

    wait_.set_object(msg_channel_.get());
    wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    return wait_.Begin(loop_.dispatcher());
}

void TrustyVirtioDevice::Stop() {
    wait_.Cancel();
    msg_channel_.reset();
}

void TrustyVirtioDevice::OnMessage(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        Stop();
        zxlogf(ERROR, "Failed to async wait on channel: %s\n",
               zx_status_get_string(status));
        return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
        for (uint64_t i = 0; i < signal->count; i++) {
            auto& shm_pool = Controller::Instance()->shm_pool();

            fbl::unique_ptr<SharedMemory> message;
            status = shm_pool.Allocate(kQueueElementSize, &message);
            if (status != ZX_OK) {
                Stop();
                zxlogf(ERROR, "Failed to allocate shared memory: %s\n", zx_status_get_string(status));
                return;
            }

            uint16_t id;
            vring_desc* desc = tx_ring_.AllocDescChain(1, &id);
            if (!desc) {
                zxlogf(ERROR, "Run out of tx ring descriptor, should wait for a free one\n");
                break;
            }

            uint32_t actual_bytes;
            status = msg_channel_.read(0, (void*)message->vaddr(), (uint32_t)message->size(),
                                       &actual_bytes, nullptr, 0, nullptr);
            if (status == ZX_ERR_SHOULD_WAIT) {
                break;
            }
            if (status != ZX_OK) {
                Stop();
                zxlogf(ERROR, "Failed to read channel: %s\n", zx_status_get_string(status));
                return;
            }

            desc->addr = message->paddr();
            desc->len = actual_bytes;

            tx_ring_.SubmitChain(id);
            tx_buf_list_.push_back(fbl::move(message));
        }

        tx_ring_.Kick();

        status = wait->Begin(loop_.dispatcher());
        if (status != ZX_OK) {
            zxlogf(ERROR, "Failed to async wait on channel: %s\n",
                   zx_status_get_string(status));
            Stop();
        }
        return;
    }

    // This will be observed after we drained all messages from the channel
    ZX_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    zxlogf(ERROR, "Peer closed\n");
    Stop();
}

void TrustyVirtioDevice::IrqRingUpdate() {
    auto free_tx_msg = [this](vring_used_elem* used_elem) {
        auto id = (uint16_t)used_elem->id;
        struct vring_desc* desc = tx_ring_.DescFromIndex(id);

        tx_buf_list_.erase_if([desc](const SharedMemory& shm) {
            return (desc->addr == shm.paddr());
        });

        tx_ring_.FreeDesc(id);
    };
    tx_ring_.IrqRingUpdate(free_tx_msg);

    bool need_kick = false;
    auto receive_rx_msg = [this, &need_kick](vring_used_elem* used_elem) {
        auto id = (uint16_t)used_elem->id;
        struct vring_desc* desc = rx_ring_.DescFromIndex(id);

        auto shm = rx_buf_list_.find_if([desc](const SharedMemory& shm) {
            return (desc->addr == shm.paddr());
        });
        ZX_ASSERT(shm != rx_buf_list_.end());
        ZX_ASSERT(msg_channel_);

        zx_status_t status = msg_channel_.write(0, (void*)shm->vaddr(),
                                                (uint32_t)used_elem->len, nullptr, 0);
        ZX_ASSERT(status == ZX_OK);

        rx_ring_.FreeDesc(id);

        desc = rx_ring_.AllocDescChain(1, &id);
        desc->addr = shm->paddr();
        desc->len = kQueueElementSize;
        desc->flags |= VRING_DESC_F_WRITE;
        rx_ring_.SubmitChain(id);

        need_kick = true;
    };

    rx_ring_.IrqRingUpdate(receive_rx_msg);
    if (need_kick) {
        rx_ring_.Kick();
    }
}

void TrustyVirtioDevice::IrqConfigChange() {}

} // namespace trusty_virtio
