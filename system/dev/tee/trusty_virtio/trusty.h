// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared_memory.h"
#include "device.h"
#include "ring.h"

namespace trusty_virtio {

class TrustyVirtioDevice : public Device {
public:
    TrustyVirtioDevice(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend);

    zx_status_t Init() override;
    void Release() override;
    void Unbind() override;

    void IrqRingUpdate() override;
    void IrqConfigChange() override;

    const char* tag() const override { return "virtio-trusty"; };

private:
    constexpr static uint16_t kRxQueueId = 0u;
    constexpr static uint16_t kTxQueueId = 1u;
    constexpr static size_t kQueueSize = 16;
    constexpr static size_t kQueueElementSize = 64 * 1024;

    using RxBufferList = fbl::DoublyLinkedList<fbl::unique_ptr<SharedMemory>>;
    RxBufferList rx_buf_list_;

    Ring tx_ring_ {this};
    Ring rx_ring_ {this};
};

} // namespace trusty_virtio
