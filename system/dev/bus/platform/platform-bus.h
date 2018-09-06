// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clk.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c-impl.h>
#include <ddktl/protocol/iommu.h>
#include <ddktl/protocol/platform-bus.h>
#include <fbl/array.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/sync/completion.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

#include "platform-device.h"
#include "platform-protocol-device.h"
#include "platform-i2c.h"
#include "proxy-protocol.h"

namespace platform_bus {

class PlatformBus;
using PlatformBusType = ddk::Device<PlatformBus, ddk::GetProtocolable>;

// This is the main class for the platform bus driver.
class PlatformBus : public PlatformBusType, public ddk::PlatformBusProtocol<PlatformBus>,
                    public ddk::IommuProtocol<PlatformBus> {
public:
    static zx_status_t Create(zx_device_t* parent, const char* name, zx::vmo zbi);

    zx_status_t Proxy(platform_proxy_args_t* args);

    // Device protocol implementation.
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    void DdkRelease();

    // Platform bus protocol implementation.
    zx_status_t DeviceAdd(const pbus_dev_t* dev);
    zx_status_t ProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev);
    zx_status_t RegisterProtocol(uint32_t proto_id, void* protocol, platform_proxy_cb_t proxy_cb,
                                 void* proxy_cb_cookie);
    const char* GetBoardName();
    zx_status_t SetBoardInfo(const pbus_board_info_t* info);

    // IOMMU protocol implementation.
    zx_status_t GetBti(uint32_t iommu_index, uint32_t bti_id, zx_handle_t* out_handle);

    // Returns the resource handle to be used for creating MMIO regions and IRQs.
    // Currently this just returns the root resource, but we may change this to a more
    // limited resource in the future.
    zx_handle_t GetResource() const { return get_root_resource(); }

    // Used by PlatformDevice to queue I2C transactions on an I2C bus.
    zx_status_t I2cTransact(uint32_t txid, rpc_i2c_req_t* req, const pbus_i2c_channel_t* channel,
                            const void* write_buf, zx_handle_t channel_handle);

    // Helper for PlatformDevice.
    zx_status_t GetBoardInfo(pdev_board_info_t* out_info);

    zx_status_t GetZbiMetadata(uint32_t type, uint32_t extra, const void** out_metadata,
                               uint32_t* out_size);

    // Protocol accessors for PlatformDevice.
    inline ddk::ClkProtocolProxy* clk() const { return clk_.get(); }
    inline ddk::GpioProtocolProxy* gpio() const { return gpio_.get(); }
    inline ddk::I2cImplProtocolProxy* i2c_impl() const { return i2c_impl_.get(); }

private:
    // This class is a wrapper for a platform_proxy_cb_t added via pbus_register_protocol().
    // It also is the element type for the proto_proxys_ WAVL tree.
    class ProtoProxy : public fbl::WAVLTreeContainable<fbl::unique_ptr<ProtoProxy>> {
    public:
        ProtoProxy(uint32_t proto_id, ddk::AnyProtocol* protocol, platform_proxy_cb_t proxy_cb,
                   void* proxy_cb_cookie)
            : proto_id_(proto_id), protocol_(*protocol), proxy_cb_(proxy_cb),
              proxy_cb_cookie_(proxy_cb_cookie) {}

        inline uint32_t GetKey() const { return proto_id_; }
        inline void GetProtocol(void* out) const { memcpy(out, &protocol_, sizeof(protocol_)); }

        inline zx_status_t Proxy(platform_proxy_args_t* args) {
            return proxy_cb_(args, proxy_cb_cookie_);
        }

    private:
        const uint32_t proto_id_;
        ddk::AnyProtocol protocol_;
        const platform_proxy_cb_t proxy_cb_;
        void* proxy_cb_cookie_;
    };


    explicit PlatformBus(zx_device_t* parent);

    DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformBus);

    zx_status_t Init(zx::vmo zbi);

    // Reads the platform ID and driver metadata records from the boot image.
    zx_status_t ReadZbi(zx::vmo zbi);

    zx_status_t I2cInit(i2c_impl_protocol_t* i2c);

    pdev_board_info_t board_info_;

    // Protocols that are optionally provided by the board driver.
    fbl::unique_ptr<ddk::ClkProtocolProxy> clk_;
    fbl::unique_ptr<ddk::GpioProtocolProxy> gpio_;
    fbl::unique_ptr<ddk::IommuProtocolProxy> iommu_;
    fbl::unique_ptr<ddk::I2cImplProtocolProxy> i2c_impl_;

    // Completion used by WaitProtocol().
    sync_completion_t proto_completion_ __TA_GUARDED(proto_completion_mutex_);
    // Protects proto_completion_.
    fbl::Mutex proto_completion_mutex_;

    // Metadata extracted from ZBI.
    fbl::Array<uint8_t> metadata_;

    // List of I2C buses.
    fbl::Vector<fbl::unique_ptr<PlatformI2cBus>> i2c_buses_;

    // Dummy IOMMU.
    zx::handle iommu_handle_;

    fbl::WAVLTree<uint32_t, fbl::unique_ptr<ProtoProxy>> proto_proxys_
                                                __TA_GUARDED(proto_proxys_mutex_);
    // Protects proto_proxys_.
    fbl::Mutex proto_proxys_mutex_;
};

} // namespace platform_bus

__BEGIN_CDECLS
zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name,
                                const char* args, zx_handle_t rpc_channel);
__END_CDECLS
