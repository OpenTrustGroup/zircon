// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trusty.h"

#include <string.h>

#include <fbl/auto_call.h>
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
    zx_status_t status = rx_ring_.Init(kRxQueueId, kQueueSize);
    if (status) {
        TRACEF("failed to initialize Rx ring: %s\n", zx_status_get_string(status));
        return status;
    }

    status = tx_ring_.Init(kTxQueueId, kQueueSize);
    if (status) {
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

        desc->addr = shm->paddr();
        desc->len = kQueueElementSize;
        desc->flags |= VRING_DESC_F_WRITE;

        rx_ring_.SubmitChain(id);
        rx_buf_list_.push_back(fbl::move(shm));
    }

    // Start the interrupt thread and set the driver OK status
    StartIrqThread();

    // Give the rx buffers to the host
    rx_ring_.Kick();

    // Initialize the zx_device and publish us
    device_add_args_t args;
    memset(&args, 0 ,sizeof(args));
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

void TrustyVirtioDevice::Release() {}
void TrustyVirtioDevice::Unbind() {}

void TrustyVirtioDevice::IrqRingUpdate() {}
void TrustyVirtioDevice::IrqConfigChange() {}

} // namespace trusty_virtio
