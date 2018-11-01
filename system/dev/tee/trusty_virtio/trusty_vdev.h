// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/device.h>
#include <ddktl/protocol/tee.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include "device.h"
#include "ring.h"
#include "shared_memory.h"

namespace trusty_virtio {

class TrustyVirtioDevice;
using DeviceType = ddk::Device<TrustyVirtioDevice, ddk::Closable, ddk::Ioctlable>;
using Protocol = ddk::TeeProtocol<TrustyVirtioDevice>;

class TrustyVirtioDevice : public DeviceType, public Protocol, public virtio::Device {
public:
    TrustyVirtioDevice(zx_device_t* bus_device, zx::bti bti,
                       fbl::unique_ptr<virtio::Backend> backend);

    virtual ~TrustyVirtioDevice();

    zx_status_t Init() override;

    zx_status_t DdkClose(uint32_t flags);
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

    void IrqRingUpdate() override;
    void IrqConfigChange() override;

    const char* tag() const override { return "virtio-trusty"; }

    zx_handle_t msg_channel() { return msg_channel_.get(); }
    zx_status_t Start(zx::channel msg_channel);
    void Stop();

private:
    constexpr static uint16_t kRxQueueId = 0u;
    constexpr static uint16_t kTxQueueId = 1u;
    constexpr static size_t kQueueSize = 16;
    constexpr static size_t kQueueElementSize = 64 * 1024;

    zx::channel msg_channel_;

    void OnMessage(async_dispatcher_t* dispatcher, async::WaitBase* self,
                   zx_status_t status, const zx_packet_signal_t* signal);

    async::WaitMethod<TrustyVirtioDevice, &TrustyVirtioDevice::OnMessage> wait_{this};
    async::Loop loop_;
    thrd_t loop_thread_;

    using BufferList = fbl::DoublyLinkedList<fbl::unique_ptr<SharedMemory>>;
    BufferList rx_buf_list_;
    BufferList tx_buf_list_;

    Ring tx_ring_{this};
    Ring rx_ring_{this};
};

} // namespace trusty_virtio
