// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rtl8211f.h"
#include "mdio-regs.h"
#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace phy {

zx_status_t PhyDevice::ConfigPhy(void* ctx,uint8_t* mac, uint8_t len) {
    uint32_t val;
    auto& self = *static_cast<PhyDevice*>(ctx);

    if (len != MAC_ARRAY_LENGTH) {
        return ZX_ERR_INVALID_ARGS;
    }

    // WOL reset.
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0xd40);
    mdio_write(&self.eth_mac_, 22, 0x20);
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0);

    mdio_write(&self.eth_mac_, MII_EPAGSR, 0xd8c);
    mdio_write(&self.eth_mac_, 16, (mac[1] << 8) | mac[0]);
    mdio_write(&self.eth_mac_, 17, (mac[3] << 8) | mac[2]);
    mdio_write(&self.eth_mac_, 18, (mac[5] << 8) | mac[4]);
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0);

    mdio_write(&self.eth_mac_, MII_EPAGSR, 0xd8a);
    mdio_write(&self.eth_mac_, 17, 0x9fff);
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0);

    mdio_write(&self.eth_mac_, MII_EPAGSR, 0xd8a);
    mdio_write(&self.eth_mac_, 16, 0x1000);
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0);

    mdio_write(&self.eth_mac_, MII_EPAGSR, 0xd80);
    mdio_write(&self.eth_mac_, 16, 0x3000);
    mdio_write(&self.eth_mac_, 17, 0x0020);
    mdio_write(&self.eth_mac_, 18, 0x03c0);
    mdio_write(&self.eth_mac_, 19, 0x0000);
    mdio_write(&self.eth_mac_, 20, 0x0000);
    mdio_write(&self.eth_mac_, 21, 0x0000);
    mdio_write(&self.eth_mac_, 22, 0x0000);
    mdio_write(&self.eth_mac_, 23, 0x0000);
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0);

    mdio_write(&self.eth_mac_, MII_EPAGSR, 0xd8a);
    mdio_write(&self.eth_mac_, 19, 0x1002);
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0);

    // Fix txdelay issuee for rtl8211.  When a hw reset is performed
    // on the phy, it defaults to having an extra delay in the TXD path.
    // Since we reset the phy, this needs to be corrected.
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0xd08);
    mdio_read(&self.eth_mac_, 0x11, &val);
    val &= ~0x100;
    mdio_write(&self.eth_mac_, 0x11, val);
    mdio_write(&self.eth_mac_, MII_EPAGSR, 0x00);

    // Enable GigE advertisement.
    mdio_write(&self.eth_mac_, MII_GBCR, 1 << 9);

    // Restart advertisements.
    mdio_read(&self.eth_mac_, MII_BMCR, &val);
    val |= BMCR_ANENABLE | BMCR_ANRESTART;
    val &= ~BMCR_ISOLATE;
    mdio_write(&self.eth_mac_, MII_BMCR, val);

    return ZX_OK;
}

static void DdkUnbind(void* ctx) {
    auto& self = *static_cast<PhyDevice*>(ctx);
    device_remove(self.device_);
}

static void DdkRelease(void* ctx) {
    delete static_cast<PhyDevice*>(ctx);
}

static zx_protocol_device_t device_ops = []() {
    zx_protocol_device_t result;

    result.version = DEVICE_OPS_VERSION;
    result.unbind = &DdkUnbind;
    result.release = &DdkRelease;
    return result;
}();

static device_add_args_t phy_device_args = []() {
    device_add_args_t result;
    result.name = "phy_null_device",
    result.version = DEVICE_ADD_ARGS_VERSION;
    result.flags = DEVICE_ADD_NON_BINDABLE;
    result.ops = &device_ops;
    return result;
}();

zx_status_t PhyDevice::Create(zx_device_t* device) {
    fbl::AllocChecker ac;
    auto phy_device = fbl::make_unique_checked<PhyDevice>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Get ETH_MAC protocol.
    zx_status_t status = device_get_protocol(device,
                                             ZX_PROTOCOL_ETH_MAC,
                                             &phy_device->eth_mac_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not obtain ETH_BOARD protocol: %d\n", status);
        return status;
    }

    status = device_add(device, &phy_device_args, &phy_device->device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwmac: Could not create phy device: %d\n", status);
        return status;
    }

    eth_mac_callbacks_t cb;
    cb.config_phy = PhyDevice::ConfigPhy;
    cb.ctx = phy_device.get();

    register_callbacks(&phy_device->eth_mac_, &cb);
    return status;
}

} // namespace phy

extern "C" zx_status_t rtl8211f_bind(void* ctx, zx_device_t* device) {
    return phy::PhyDevice::Create(device);
}
