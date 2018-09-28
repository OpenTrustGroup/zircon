// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>
#include <zircon/compiler.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <ddktl/device.h>

#include "aml-dsi.h"
#include "dw-mipi-dsi-reg.h"
#include "common.h"

namespace astro_display {

class AmlMipiPhy {
public:
    AmlMipiPhy() {}
    ~AmlMipiPhy() {
        io_buffer_release(&mmio_mipi_dsi_);
        io_buffer_release(&mmio_dsi_phy_);
    }
    // This function initializes internal state of the object
    zx_status_t Init(zx_device_t* parent, uint32_t lane_num);
    // This function enables and starts up the Mipi Phy
    zx_status_t Startup();
    // This function stops Mipi Phy
    void Shutdown();
    zx_status_t PhyCfgLoad(uint32_t bitrate);
    void Dump();
    uint32_t GetLowPowerEscaseTime() {
        return dsi_phy_cfg_.lp_tesc;
    }
private:
    // This structure holds the timing parameters used for MIPI D-PHY
    //This can be moved later on to MIPI D-PHY specific header if need be
    struct DsiPhyConfig {
        uint32_t lp_tesc;
        uint32_t lp_lpx;
        uint32_t lp_ta_sure;
        uint32_t lp_ta_go;
        uint32_t lp_ta_get;
        uint32_t hs_exit;
        uint32_t hs_trail;
        uint32_t hs_zero;
        uint32_t hs_prepare;
        uint32_t clk_trail;
        uint32_t clk_post;
        uint32_t clk_zero;
        uint32_t clk_prepare;
        uint32_t clk_pre;
        uint32_t init;
        uint32_t wakeup;
    };

    void PhyInit();
    zx_status_t WaitforPhyReady();

    io_buffer_t                                 mmio_mipi_dsi_;
    io_buffer_t                                 mmio_dsi_phy_;
    platform_device_protocol_t                  pdev_ = {};
    fbl::unique_ptr<hwreg::RegisterIo>          mipi_dsi_regs_;
    fbl::unique_ptr<hwreg::RegisterIo>          dsi_phy_regs_;
    uint32_t                                    num_of_lanes_;
    DsiPhyConfig                                dsi_phy_cfg_;

    bool                                        initialized_ = false;
    bool                                        phy_enabled_ = false;
};

} // namespace astro_display
