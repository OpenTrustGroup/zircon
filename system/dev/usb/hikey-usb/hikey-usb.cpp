// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hikey-usb.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>

namespace hikey_usb {

zx_status_t HikeyUsb::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto bus = fbl::make_unique_checked<HikeyUsb>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = bus->Init();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = bus.release();
    return ZX_OK;
}

zx_status_t HikeyUsb::Init() {
    platform_device_protocol_t pdev;

    auto status = device_get_protocol(parent(), ZX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (status != ZX_OK) {
        return status;
    }
    for (uint32_t i = 0; i < countof(gpios_); i++) {
        status = pdev_get_protocol(&pdev, ZX_PROTOCOL_GPIO, i, &gpios_[i]);
        if (status != ZX_OK) {
            return status;
        }
        gpio_config_out(&gpios_[i], 0);
    }

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
        {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_DWC3},
    };

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "dwc3";
    args.ctx = this;
    args.ops = &ddk_device_proto_;
    args.props = props;
    args.prop_count = static_cast<uint32_t>(fbl::count_of(props));
    args.proto_id = ddk_proto_id_;
    args.proto_ops = ddk_proto_ops_;

    return pdev_device_add(&pdev, 0, &args, &zxdev_);
}

zx_status_t HikeyUsb::UmsSetMode(usb_mode_t mode) {
    if (mode == usb_mode_) {
        return ZX_OK;
    }
    if (mode == USB_MODE_OTG) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    gpio_write(&gpios_[HUB_VDD33_EN], mode == USB_MODE_HOST);
    gpio_write(&gpios_[VBUS_TYPEC], mode == USB_MODE_HOST);
    gpio_write(&gpios_[USBSW_SW_SEL], mode == USB_MODE_HOST);

    usb_mode_ = mode;
    return ZX_OK;


    return ZX_OK;
}

void HikeyUsb::DdkRelease() {
    delete this;
}

} // namespace hikey_usb

zx_status_t hikey_usb_bind(void* ctx, zx_device_t* parent) {
    return hikey_usb::HikeyUsb::Create(parent);
}
