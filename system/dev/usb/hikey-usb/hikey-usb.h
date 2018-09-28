// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/usb-mode-switch.h>
#include <fbl/macros.h>

namespace hikey_usb {

class HikeyUsb;
using HikeyUsbType = ddk::Device<HikeyUsb>;

// This is the main class for the platform bus driver.
class HikeyUsb : public HikeyUsbType, public ddk::UmsProtocol<HikeyUsb> {
public:
    explicit HikeyUsb(zx_device_t* parent)
        : HikeyUsbType(parent), usb_mode_(USB_MODE_NONE) {}

    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    void DdkRelease();

    // USB mode switch protocol implementation.
    zx_status_t UmsSetMode(usb_mode_t mode);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(HikeyUsb);

    enum {
        HUB_VDD33_EN,
        VBUS_TYPEC,
        USBSW_SW_SEL,
        GPIO_COUNT,
    };

    zx_status_t Init();

    gpio_protocol_t gpios_[GPIO_COUNT];
    usb_mode_t usb_mode_;
};

} // namespace hikey_usb

__BEGIN_CDECLS
zx_status_t hikey_usb_bind(void* ctx, zx_device_t* parent);
__END_CDECLS
