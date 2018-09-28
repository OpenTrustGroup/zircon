// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>
#include <zircon/compiler.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <ddk/protocol/gpio.h>
#include "dw-mipi-dsi.h"

namespace astro_display {

class Lcd {
public:
    Lcd(uint8_t panel_type) : panel_type_(panel_type) {}

    zx_status_t Init(zx_device_t* parent);
    zx_status_t Enable();
    zx_status_t Disable();
private:
    zx_status_t LoadInitTable(const uint8_t* buffer, size_t size);
    zx_status_t GetDisplayId();

    uint8_t                                     panel_type_;
    gpio_protocol_t                             gpio_ = {};
    fbl::unique_ptr<astro_display::DwMipiDsi>   dsi_;

    bool                                        initialized_ = false;
    bool                                        enabled_ =false;
};

} // namespace astro_display
