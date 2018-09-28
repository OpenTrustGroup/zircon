// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>
#include <zircon/compiler.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <ddktl/device.h>

#include "aml-dsi.h"
#include "hhi-regs.h"
#include "vpu-regs.h"
#include "common.h"

namespace astro_display {

class AstroDisplayClock {
public:
    AstroDisplayClock() {}
    ~AstroDisplayClock() {
        io_buffer_release(&mmio_vpu_);
        io_buffer_release(&mmio_hhi_);
    }
    zx_status_t Init(zx_device_t* parent);
    zx_status_t Enable(const DisplaySetting& d);
    void Disable();
    void Dump();

    uint32_t GetBitrate() {
        return pll_cfg_.bitrate;
    }

private:
    void CalculateLcdTiming(const DisplaySetting& disp_setting);

    // This function wait for hdmi_pll to lock. The retry algorithm is
    // undocumented and comes from U-Boot.
    zx_status_t PllLockWait();

    // This function calculates the required pll configurations needed to generate
    // the desired lcd clock
    zx_status_t GenerateHPLL(const DisplaySetting& disp_setting);

    io_buffer_t                             mmio_vpu_;
    io_buffer_t                             mmio_hhi_;
    platform_device_protocol_t              pdev_ = {};
    fbl::unique_ptr<hwreg::RegisterIo>      vpu_regs_;
    fbl::unique_ptr<hwreg::RegisterIo>      hhi_regs_;

    PllConfig                               pll_cfg_;
    LcdTiming                               lcd_timing_;

    bool                                    initialized_ = false;
    bool                                    clock_enabled_ = false;
};

} // namespace astro_display
