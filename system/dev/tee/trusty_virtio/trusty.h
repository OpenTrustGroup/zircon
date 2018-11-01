// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include "device.h"
#include "ring.h"
#include "shared_memory.h"

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

    void Stop() { wait_.Cancel(); }

private:
    constexpr static uint16_t kRxQueueId = 0u;
    constexpr static uint16_t kTxQueueId = 1u;
    constexpr static size_t kQueueSize = 16;
    constexpr static size_t kQueueElementSize = 64 * 1024;

    zx::channel client_channel_;
    zx::channel server_channel_;

    void OnMessage(async_dispatcher_t* dispatcher, async::WaitBase* self,
                   zx_status_t status, const zx_packet_signal_t* signal);

    async::WaitMethod<TrustyVirtioDevice, &TrustyVirtioDevice::OnMessage> wait_{this};

    using BufferList = fbl::DoublyLinkedList<fbl::unique_ptr<SharedMemory>>;
    BufferList rx_buf_list_;
    BufferList tx_buf_list_;

    Ring tx_ring_{this};
    Ring rx_ring_{this};
};

} // namespace trusty_virtio
