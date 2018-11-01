// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoteproc.h"

#include <zircon/status.h>

#include "controller.h"

namespace trusty_virtio {

zx_status_t RemoteProc::Bind() {
    return ZX_ERR_NOT_SUPPORTED;
}

void RemoteProc::Unbind() {
}

// Returns true if the specified feature bit is set
bool RemoteProc::ReadFeature(uint32_t bit) {
    return (descr_->vdev.dfeatures & bit);
}

// Does a Driver -> Device acknowledgement of a feature bit
void RemoteProc::SetFeature(uint32_t bit) {
    descr_->vdev.gfeatures |= bit;
}

// Does a FEATURES_OK check
zx_status_t RemoteProc::ConfirmFeatures() {
    return ZX_OK;
}

// Device lifecycle methods
void RemoteProc::DriverStatusOk() {
    descr_->vdev.status |= (VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
}

void RemoteProc::DriverStatusAck() {
    descr_->vdev.status |= VIRTIO_STATUS_ACKNOWLEDGE;
}

void RemoteProc::DeviceReset() {
}

uint16_t RemoteProc::GetRingSize(uint16_t index) {
    ZX_ASSERT(index < VIRTIO_TRUSTY_NUM_QUEUES);
    return static_cast<uint16_t>(descr_->vrings[index].num);
}

void RemoteProc::SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc,
                         zx_paddr_t pa_avail, zx_paddr_t pa_used) {
    ZX_ASSERT(index < VIRTIO_TRUSTY_NUM_QUEUES);
    descr_->vrings[index].da = static_cast<uint32_t>(pa_desc);
    descr_->vrings[index].num = static_cast<uint32_t>(count);
}

void RemoteProc::RingKick(uint16_t ring_index) {
    auto controller = Controller::Instance();

    zx_status_t status = controller->monitor_nop_call(SMC_NC_VDEV_KICK_VQ,
                                                      notify_id_, ring_index);
    if (status == ZX_OK) {
        isr_status_ |= VIRTIO_ISR_QUEUE_INT;
        irq_event_.signal(0, ZX_EVENT_SIGNALED);
    } else {
        TRACEF("Failed to kick vq: %s\n", zx_status_get_string(status));
    }
}

} // namespace trusty_virtio
