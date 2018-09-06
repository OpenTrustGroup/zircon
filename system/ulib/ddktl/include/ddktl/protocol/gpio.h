// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "gpio-internal.h"

// DDK GPIO protocol support.
//
// :: Proxies ::
//
// ddk::GpioProtocolProxy is a simple wrappers around gpio_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::GpioProtocol is a mixin class that simplifies writing DDK drivers that
// implement the GPIO protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_GPIO device.
// class GpioDevice;
// using GpioDeviceType = ddk::Device<GpioDevice, /* ddk mixins */>;
//
// class GpioDevice : public GpioDeviceType,
//                    public ddk::GpioProtocol<GpioDevice> {
//   public:
//     GpioDevice(zx_device_t* parent)
//       : GpioDeviceType("my-gpio-device", parent) {}
//
//     zx_status_t GpioConfigIn(uint32_t index, uint32_t flags);
//     zx_status_t GpioConfigOut(uint32_t index, uint8_t initial_value);
//     zx_status_t GpioSetAltFunction(uint32_t index, uint64_t function);
//     zx_status_t GpioRead(uint32_t index, uint8_t* out_value);
//     zx_status_t GpioWrite(uint32_t index, uint8_t value);
//     zx_status_t GpioGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t *out_handle);
//     zx_status_t GpioReleaseInterrupt(uint32_t index);
//     zx_status_t GpioSetPolarity(uint32_t index, uint32_t polarity);
//     ...
// };

namespace ddk {

template <typename D>
class GpioProtocol : public internal::base_protocol {
public:
    GpioProtocol() {
        internal::CheckGpioProtocolSubclass<D>();
        ops_.config_in = GpioConfigIn;
        ops_.config_out = GpioConfigOut;
        ops_.set_alt_function = GpioSetAltFunction;
        ops_.read = GpioRead;
        ops_.write = GpioWrite;
        ops_.get_interrupt = GpioGetInterrupt;
        ops_.release_interrupt = GpioReleaseInterrupt;
        ops_.set_polarity = GpioSetPolarity;

        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_GPIO;
        ddk_proto_ops_ = &ops_;
    }

protected:
    gpio_protocol_ops_t ops_ = {};

private:
    static zx_status_t GpioConfigIn(void* ctx, uint32_t index, uint32_t flags) {
        return static_cast<D*>(ctx)->GpioConfigIn(index, flags);
    }
    static zx_status_t GpioConfigOut(void* ctx, uint32_t index, uint8_t initial_value) {
        return static_cast<D*>(ctx)->GpioConfigOut(index, initial_value);
    }
    static zx_status_t GpioSetAltFunction(void* ctx, uint32_t index, uint64_t function) {
        return static_cast<D*>(ctx)->GpioSetAltFunction(index, function);
    }
    static zx_status_t GpioRead(void* ctx, uint32_t index, uint8_t* out_value) {
        return static_cast<D*>(ctx)->GpioRead(index, out_value);
    }
    static zx_status_t GpioWrite(void* ctx, uint32_t index, uint8_t value) {
        return static_cast<D*>(ctx)->GpioWrite(index, value);
    }
    static zx_status_t GpioGetInterrupt(void* ctx, uint32_t index, uint32_t flags,
                                        zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->GpioGetInterrupt(index, flags, out_handle);
    }
    static zx_status_t GpioReleaseInterrupt(void* ctx, uint32_t index) {
        return static_cast<D*>(ctx)->GpioReleaseInterrupt(index);
    }
    static zx_status_t GpioSetPolarity(void* ctx, uint32_t index, uint32_t polarity) {
        return static_cast<D*>(ctx)->GpioSetPolarity(index, polarity);
    }
};

class GpioProtocolProxy {
public:
    GpioProtocolProxy(gpio_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(gpio_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }

    zx_status_t ConfigIn(uint32_t index, uint32_t flags) {
        return ops_->config_in(ctx_, index, flags);
    }
    zx_status_t ConfigOut(uint32_t index, uint8_t initial_value) {
        return ops_->config_out(ctx_, index, initial_value);
    }
    zx_status_t SetAltFunction(uint32_t index, uint64_t function) {
        return ops_->set_alt_function(ctx_, index, function);
    }
    zx_status_t Read(uint32_t index, uint8_t* out_value) {
        return ops_->read(ctx_, index, out_value);
    }
    zx_status_t Write(uint32_t index, uint8_t value) {
        return ops_->write(ctx_, index, value);
    }
    zx_status_t GetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle) {
        return ops_->get_interrupt(ctx_, index, flags, out_handle);
    }
    zx_status_t ReleaseInterrupt(uint32_t index) {
        return ops_->release_interrupt(ctx_, index);
    }
    zx_status_t SetPolarity(uint32_t index, uint32_t polarity) {
        return ops_->set_polarity(ctx_, index, polarity);
    }

private:
    gpio_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
