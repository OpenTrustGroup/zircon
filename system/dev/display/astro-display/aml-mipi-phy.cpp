// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-mipi-phy.h"
#include <ddk/debug.h>

namespace astro_display {

#define READ32_MIPI_DSI_REG(a)              mipi_dsi_regs_->Read<uint32_t>(a)
#define WRITE32_MIPI_DSI_REG(a, v)          mipi_dsi_regs_->Write<uint32_t>(a, v)

#define READ32_DSI_PHY_REG(a)               dsi_phy_regs_->Read<uint32_t>(a)
#define WRITE32_DSI_PHY_REG(a, v)           dsi_phy_regs_->Write<uint32_t>(a, v)

template<typename T>
constexpr inline uint8_t NsToLaneByte(T x, uint32_t lanebytetime) {
    return (static_cast<uint8_t>((x + lanebytetime - 1) / lanebytetime) & 0xFF);
}

constexpr uint32_t kUnit = (1 * 1000 * 1000 * 100);
constexpr uint32_t kPhyDelay = 6;

zx_status_t AmlMipiPhy::PhyCfgLoad(uint32_t bitrate) {
    ZX_DEBUG_ASSERT(initialized_);

    // According to MIPI -PHY Spec, we need to define Unit Interval (UI).
    // This UI is defined as the time it takes to send a bit (i.e. bitrate)
    // The x100 is to ensure the ui is not rounded too much (i.e. 2.56 --> 256)
    // However, since we have introduced x100, we need to make sure we include x100
    // to all the PHY timings that are in ns units.
    const uint32_t ui = kUnit / (bitrate / 1000);

    // Calculate values will be rounded by the lanebyteclk
    const uint32_t lanebytetime = ui * 8;

    // lp_tesc:TX Excape Clock Division factor (from linebyteclk). Round up to units of ui
    dsi_phy_cfg_.lp_tesc = NsToLaneByte(DPHY_TIME_LP_TESC, lanebytetime);

    // lp_lpx: Transmit length of any LP state period
    dsi_phy_cfg_.lp_lpx = NsToLaneByte(DPHY_TIME_LP_LPX, lanebytetime);

    // lp_ta_sure
    dsi_phy_cfg_.lp_ta_sure = NsToLaneByte(DPHY_TIME_LP_TA_SURE, lanebytetime);

    // lp_ta_go
    dsi_phy_cfg_.lp_ta_go = NsToLaneByte(DPHY_TIME_LP_TA_GO, lanebytetime);

    // lp_ta_get
    dsi_phy_cfg_.lp_ta_get = NsToLaneByte(DPHY_TIME_LP_TA_GET, lanebytetime);

    // hs_exit
    dsi_phy_cfg_.hs_exit = NsToLaneByte(DPHY_TIME_HS_EXIT, lanebytetime);

    // clk-_prepare
    dsi_phy_cfg_.clk_prepare = NsToLaneByte(DPHY_TIME_CLK_PREPARE, lanebytetime);

    // clk_zero
    dsi_phy_cfg_.clk_zero = NsToLaneByte(DPHY_TIME_CLK_ZERO(ui), lanebytetime);

    // clk_pre
    dsi_phy_cfg_.clk_pre = NsToLaneByte(DPHY_TIME_CLK_PRE(ui), lanebytetime);

    // init
    dsi_phy_cfg_.init = NsToLaneByte(DPHY_TIME_INIT, lanebytetime);

    // wakeup
    dsi_phy_cfg_.wakeup = NsToLaneByte(DPHY_TIME_WAKEUP, lanebytetime);

    // clk_trail
    dsi_phy_cfg_.clk_trail = NsToLaneByte(DPHY_TIME_CLK_TRAIL, lanebytetime);

    // clk_post
    dsi_phy_cfg_.clk_post = NsToLaneByte(DPHY_TIME_CLK_POST(ui), lanebytetime);

    // hs_trail
    dsi_phy_cfg_.hs_trail = NsToLaneByte(DPHY_TIME_HS_TRAIL(ui), lanebytetime);

    // hs_prepare
    dsi_phy_cfg_.hs_prepare = NsToLaneByte(DPHY_TIME_HS_PREPARE(ui), lanebytetime);

    // hs_zero
    dsi_phy_cfg_.hs_zero = NsToLaneByte(DPHY_TIME_HS_ZERO(ui), lanebytetime);

    // Ensure both clk-trail and hs-trail do not exceed Teot (End of Transmission Time)
    const uint32_t time_req_max = NsToLaneByte(DPHY_TIME_EOT(ui), lanebytetime);
    if ((dsi_phy_cfg_.clk_trail > time_req_max) ||
        (dsi_phy_cfg_.hs_trail > time_req_max)) {
        DISP_ERROR("Invalid clk-trail and/or hs-trail exceed Teot!\n");
        DISP_ERROR("clk-trail = 0x%02x, hs-trail =  0x%02x, Teot = 0x%02x\n",
                   dsi_phy_cfg_.clk_trail, dsi_phy_cfg_.hs_trail, time_req_max );
        return ZX_ERR_OUT_OF_RANGE;
    }

    DISP_SPEW("lp_tesc     = 0x%02x\n"
                "lp_lpx      = 0x%02x\n"
                "lp_ta_sure  = 0x%02x\n"
                "lp_ta_go    = 0x%02x\n"
                "lp_ta_get   = 0x%02x\n"
                "hs_exit     = 0x%02x\n"
                "hs_trail    = 0x%02x\n"
                "hs_zero     = 0x%02x\n"
                "hs_prepare  = 0x%02x\n"
                "clk_trail   = 0x%02x\n"
                "clk_post    = 0x%02x\n"
                "clk_zero    = 0x%02x\n"
                "clk_prepare = 0x%02x\n"
                "clk_pre     = 0x%02x\n"
                "init        = 0x%02x\n"
                "wakeup      = 0x%02x\n\n",
                dsi_phy_cfg_.lp_tesc,
                dsi_phy_cfg_.lp_lpx,
                dsi_phy_cfg_.lp_ta_sure,
                dsi_phy_cfg_.lp_ta_go,
                dsi_phy_cfg_.lp_ta_get,
                dsi_phy_cfg_.hs_exit,
                dsi_phy_cfg_.hs_trail,
                dsi_phy_cfg_.hs_zero,
                dsi_phy_cfg_.hs_prepare,
                dsi_phy_cfg_.clk_trail,
                dsi_phy_cfg_.clk_post,
                dsi_phy_cfg_.clk_zero,
                dsi_phy_cfg_.clk_prepare,
                dsi_phy_cfg_.clk_pre,
                dsi_phy_cfg_.init,
                dsi_phy_cfg_.wakeup);
    return ZX_OK;
}

void AmlMipiPhy::PhyInit()
{
    // Enable phy clock.
    WRITE32_REG(DSI_PHY, MIPI_DSI_PHY_CTRL, PHY_CTRL_TXDDRCLK_EN |
                PHY_CTRL_DDRCLKPATH_EN | PHY_CTRL_CLK_DIV_COUNTER | PHY_CTRL_CLK_DIV_EN |
                PHY_CTRL_BYTECLK_EN);

    // Toggle PHY CTRL RST
    SET_BIT32(DSI_PHY, MIPI_DSI_PHY_CTRL, 1, PHY_CTRL_RST_START, PHY_CTRL_RST_BITS);
    SET_BIT32(DSI_PHY, MIPI_DSI_PHY_CTRL, 0, PHY_CTRL_RST_START, PHY_CTRL_RST_BITS);

    WRITE32_REG(DSI_PHY, MIPI_DSI_CLK_TIM,
                (dsi_phy_cfg_.clk_trail | (dsi_phy_cfg_.clk_post << 8) |
                (dsi_phy_cfg_.clk_zero << 16) |
                (dsi_phy_cfg_.clk_prepare << 24)));

    WRITE32_REG(DSI_PHY, MIPI_DSI_CLK_TIM1, dsi_phy_cfg_.clk_pre);

    WRITE32_REG(DSI_PHY, MIPI_DSI_HS_TIM,
                (dsi_phy_cfg_.hs_exit | (dsi_phy_cfg_.hs_trail << 8) |
                (dsi_phy_cfg_.hs_zero << 16) |
                (dsi_phy_cfg_.hs_prepare << 24)));

    WRITE32_REG(DSI_PHY, MIPI_DSI_LP_TIM,
                (dsi_phy_cfg_.lp_lpx | (dsi_phy_cfg_.lp_ta_sure << 8) |
                (dsi_phy_cfg_.lp_ta_go << 16) | (dsi_phy_cfg_.lp_ta_get << 24)));

    WRITE32_REG(DSI_PHY, MIPI_DSI_ANA_UP_TIM, ANA_UP_TIME);
    WRITE32_REG(DSI_PHY, MIPI_DSI_INIT_TIM, dsi_phy_cfg_.init);
    WRITE32_REG(DSI_PHY, MIPI_DSI_WAKEUP_TIM, dsi_phy_cfg_.wakeup);
    WRITE32_REG(DSI_PHY, MIPI_DSI_LPOK_TIM,  LPOK_TIME);
    WRITE32_REG(DSI_PHY, MIPI_DSI_ULPS_CHECK,  ULPS_CHECK_TIME);
    WRITE32_REG(DSI_PHY, MIPI_DSI_LP_WCHDOG,  LP_WCHDOG_TIME);
    WRITE32_REG(DSI_PHY, MIPI_DSI_TURN_WCHDOG,  TURN_WCHDOG_TIME);

    WRITE32_REG(DSI_PHY, MIPI_DSI_CHAN_CTRL, 0);
}


// This function checks two things in order to decide whether the PHY is
// ready or not. LOCK Bit and StopStateClk bit. According to spec, once these
// are set, PHY has completed initialization
zx_status_t AmlMipiPhy::WaitforPhyReady()
{
    int timeout = DPHY_TIMEOUT;
    while ((GET_BIT32(MIPI_DSI, DW_DSI_PHY_STATUS, PHY_STATUS_PHY_LOCK, 1) == 0) &&
           timeout--) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(kPhyDelay)));
    }
    if (timeout <= 0) {
        DISP_ERROR("Timeout! D-PHY did not lock\n");
        return ZX_ERR_TIMED_OUT;
    }

    timeout = DPHY_TIMEOUT;
    while ((GET_BIT32(MIPI_DSI, DW_DSI_PHY_STATUS, PHY_STATUS_PHY_STOPSTATECLKLANE, 1) == 0) &&
           timeout--) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(kPhyDelay)));
    }
    if (timeout <= 0) {
        DISP_ERROR("Timeout! D-PHY StopStateClk not set\n");
        return ZX_ERR_TIMED_OUT;
    }
    return ZX_OK;
}

void AmlMipiPhy::Shutdown() {
    ZX_DEBUG_ASSERT(initialized_);

    if (!phy_enabled_) {
        return;
    }

    // Power down DSI
    WRITE32_REG(MIPI_DSI, DW_DSI_PWR_UP, PWR_UP_RST);
    WRITE32_REG(DSI_PHY, MIPI_DSI_CHAN_CTRL, 0x1f);
    SET_BIT32(DSI_PHY, MIPI_DSI_PHY_CTRL, 0, 7, 1);
    phy_enabled_ = false;
}

zx_status_t AmlMipiPhy::Startup() {
    ZX_DEBUG_ASSERT(initialized_);

    if (phy_enabled_) {
        return ZX_OK;
    }

    // Power up DSI
    WRITE32_REG(MIPI_DSI, DW_DSI_PWR_UP, PWR_UP_ON);

    // Setup Parameters of DPHY
    // Below we are sending test code 0x44 with parameter 0x74. This means
    // we are setting up the phy to operate in 1050-1099 Mbps mode
    // TODO(payamm): Find out why 0x74 was selected
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL1, 0x00010044);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0, 0x2);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0, 0x0);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL1, 0x00000074);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0, 0x2);
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_TST_CTRL0, 0x0);

    // Power up D-PHY
    WRITE32_REG(MIPI_DSI, DW_DSI_PHY_RSTZ, PHY_RSTZ_PWR_UP);

    // Setup PHY Timing parameters
    PhyInit();

    // Wait for PHY to be read
    zx_status_t status;
    if ((status = WaitforPhyReady()) != ZX_OK) {
        // no need to print additional info.
        return status;
    }

    // Trigger a sync active for esc_clk
    SET_BIT32(DSI_PHY, MIPI_DSI_PHY_CTRL, 1, 1, 1);

    // Startup transfer, default lpclk
    WRITE32_REG(MIPI_DSI, DW_DSI_LPCLK_CTRL, (0x1 << LPCLK_CTRL_AUTOCLKLANE_CTRL) |
                (0x1 << LPCLK_CTRL_TXREQUESTCLKHS));

    phy_enabled_ = true;
    return ZX_OK;
}

zx_status_t AmlMipiPhy::Init(zx_device_t* parent, uint32_t lane_num) {
    if (initialized_) {
        return ZX_OK;
    }

    num_of_lanes_ = lane_num;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev_);
    if (status != ZX_OK) {
        DISP_ERROR("AmlMipiPhy: Could not get ZX_PROTOCOL_PLATFORM_DEV protocol\n");
        return status;
    }

    // Map Mipi Dsi and Dsi Phy registers
    status = pdev_map_mmio_buffer(&pdev_, MMIO_MPI_DSI, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio_mipi_dsi_);
    if (status != ZX_OK) {
        DISP_ERROR("AmlMipiPhy: Could not map MIPI DSI mmio\n");
        return status;
    }
    status = pdev_map_mmio_buffer(&pdev_, MMIO_DSI_PHY, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio_dsi_phy_);
    if (status != ZX_OK) {
        DISP_ERROR("AmlMipiPhy: Could not map DSI PHY mmio\n");
        io_buffer_release(&mmio_mipi_dsi_);
        return status;
    }

    // Create register io
    mipi_dsi_regs_ = fbl::make_unique<hwreg::RegisterIo>(io_buffer_virt(&mmio_mipi_dsi_));
    dsi_phy_regs_ = fbl::make_unique<hwreg::RegisterIo>(io_buffer_virt(&mmio_dsi_phy_));

    initialized_ = true;
    return ZX_OK;
}

void AmlMipiPhy::Dump() {
    ZX_DEBUG_ASSERT(initialized_);
    DISP_INFO("%s: DUMPING PHY REGS\n", __func__);
    DISP_INFO("MIPI_DSI_PHY_CTRL = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_PHY_CTRL));
    DISP_INFO("MIPI_DSI_CHAN_CTRL = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_CHAN_CTRL));
    DISP_INFO("MIPI_DSI_CHAN_STS = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_CHAN_STS));
    DISP_INFO("MIPI_DSI_CLK_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_CLK_TIM));
    DISP_INFO("MIPI_DSI_HS_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_HS_TIM));
    DISP_INFO("MIPI_DSI_LP_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_LP_TIM));
    DISP_INFO("MIPI_DSI_ANA_UP_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_ANA_UP_TIM));
    DISP_INFO("MIPI_DSI_INIT_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_INIT_TIM));
    DISP_INFO("MIPI_DSI_WAKEUP_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_WAKEUP_TIM));
    DISP_INFO("MIPI_DSI_LPOK_TIM = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_LPOK_TIM));
    DISP_INFO("MIPI_DSI_LP_WCHDOG = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_LP_WCHDOG));
    DISP_INFO("MIPI_DSI_ANA_CTRL = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_ANA_CTRL));
    DISP_INFO("MIPI_DSI_CLK_TIM1 = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_CLK_TIM1));
    DISP_INFO("MIPI_DSI_TURN_WCHDOG = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_TURN_WCHDOG));
    DISP_INFO("MIPI_DSI_ULPS_CHECK = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_ULPS_CHECK));
    DISP_INFO("MIPI_DSI_TEST_CTRL0 = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_TEST_CTRL0));
    DISP_INFO("MIPI_DSI_TEST_CTRL1 = 0x%x\n", READ32_REG(DSI_PHY, MIPI_DSI_TEST_CTRL1));
    DISP_INFO("\n");

    DISP_INFO("#############################\n");
    DISP_INFO("Dumping dsi_phy_cfg structure:\n");
    DISP_INFO("#############################\n");
    DISP_INFO("lp_tesc = 0x%x (%u)\n", dsi_phy_cfg_.lp_tesc,
              dsi_phy_cfg_.lp_tesc);
    DISP_INFO("lp_lpx = 0x%x (%u)\n", dsi_phy_cfg_.lp_lpx,
              dsi_phy_cfg_.lp_lpx);
    DISP_INFO("lp_ta_sure = 0x%x (%u)\n", dsi_phy_cfg_.lp_ta_sure,
              dsi_phy_cfg_.lp_ta_sure);
    DISP_INFO("lp_ta_go = 0x%x (%u)\n", dsi_phy_cfg_.lp_ta_go,
              dsi_phy_cfg_.lp_ta_go);
    DISP_INFO("lp_ta_get = 0x%x (%u)\n", dsi_phy_cfg_.lp_ta_get,
              dsi_phy_cfg_.lp_ta_get);
    DISP_INFO("hs_exit = 0x%x (%u)\n", dsi_phy_cfg_.hs_exit,
              dsi_phy_cfg_.hs_exit);
    DISP_INFO("hs_trail = 0x%x (%u)\n", dsi_phy_cfg_.hs_trail,
              dsi_phy_cfg_.hs_trail);
    DISP_INFO("hs_zero = 0x%x (%u)\n", dsi_phy_cfg_.hs_zero,
              dsi_phy_cfg_.hs_zero);
    DISP_INFO("hs_prepare = 0x%x (%u)\n", dsi_phy_cfg_.hs_prepare,
              dsi_phy_cfg_.hs_prepare);
    DISP_INFO("clk_trail = 0x%x (%u)\n", dsi_phy_cfg_.clk_trail,
              dsi_phy_cfg_.clk_trail);
    DISP_INFO("clk_post = 0x%x (%u)\n", dsi_phy_cfg_.clk_post,
              dsi_phy_cfg_.clk_post);
    DISP_INFO("clk_zero = 0x%x (%u)\n", dsi_phy_cfg_.clk_zero,
              dsi_phy_cfg_.clk_zero);
    DISP_INFO("clk_prepare = 0x%x (%u)\n", dsi_phy_cfg_.clk_prepare,
              dsi_phy_cfg_.clk_prepare);
    DISP_INFO("clk_pre = 0x%x (%u)\n", dsi_phy_cfg_.clk_pre,
              dsi_phy_cfg_.clk_pre);
    DISP_INFO("init = 0x%x (%u)\n", dsi_phy_cfg_.init,
              dsi_phy_cfg_.init);
    DISP_INFO("wakeup = 0x%x (%u)\n", dsi_phy_cfg_.wakeup,
              dsi_phy_cfg_.wakeup);
}

} // namespace astro_display
