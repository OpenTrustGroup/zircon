// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "backends/backend.h"

#include <assert.h>
#include <lib/zx/event.h>
#include <virtio/trusty.h>
#include <zircon/thread_annotations.h>

namespace trusty_virtio {

// TODO(sy): investigate if we can use virtio-pci backend
class RemoteProc : public virtio::Backend {
public:
    explicit RemoteProc(virtio_trusty_vdev_descr_t* descr)
        : descr_(descr), isr_status_(0) {
        ZX_ASSERT(zx::event::create(0, &irq_event_) == ZX_OK);

        notify_id_ = descr_->vdev.notifyid;
    }

    zx_status_t Bind() override;
    void Unbind() override;

    // Returns true if the specified feature bit is set
    bool ReadFeature(uint32_t bit) override;
    // Does a Driver -> Device acknowledgement of a feature bit
    void SetFeature(uint32_t bit) override;
    // Does a FEATURES_OK check
    zx_status_t ConfirmFeatures() override;
    // Device lifecycle methods
    void DriverStatusOk() override;
    void DriverStatusAck() override;
    void DeviceReset() override;

    //// Read/Write the device config
    void DeviceConfigRead(uint16_t offset, uint8_t* value) override {
        ZX_ASSERT(false);
    };
    void DeviceConfigRead(uint16_t offset, uint16_t* value) override {
        ZX_ASSERT(false);
    };
    void DeviceConfigRead(uint16_t offset, uint32_t* value) override {
        ZX_ASSERT(false);
    };
    void DeviceConfigRead(uint16_t offset, uint64_t* value) override {
        ZX_ASSERT(false);
    };
    void DeviceConfigWrite(uint16_t offset, uint8_t value) override {
        ZX_ASSERT(false);
    };
    void DeviceConfigWrite(uint16_t offset, uint16_t value) override {
        ZX_ASSERT(false);
    };
    void DeviceConfigWrite(uint16_t offset, uint32_t value) override {
        ZX_ASSERT(false);
    };
    void DeviceConfigWrite(uint16_t offset, uint64_t value) override {
        ZX_ASSERT(false);
    };

    // Ring methods vary based on backend due to config offsets and field sizes.
    uint16_t GetRingSize(uint16_t index) override;
    void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
                 zx_paddr_t pa_used) override;
    void RingKick(uint16_t ring_index) override;

    uint32_t IsrStatus() override {
        auto status = isr_status_;
        isr_status_ = 0;

        return status;
    };
    zx_status_t InterruptValid() override {
        return ZX_OK;
    };
    zx_status_t WaitForInterrupt() override {
        zx_status_t err = irq_event_.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), nullptr);
        if (err != ZX_OK) {
            return err;
        }

        return irq_event_.signal(ZX_EVENT_SIGNALED, 0);
    };

private:
    virtio_trusty_vdev_descr_t* descr_;
    zx::event irq_event_;
    uint32_t isr_status_;
    uint32_t notify_id_;
};

} // namespace trusty_virtio
