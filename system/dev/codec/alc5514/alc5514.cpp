// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include <zircon/device/audio-codec.h>
#include <zircon/device/i2c.h>
#include <zircon/assert.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "alc5514.h"
#include "alc5514-registers.h"

namespace audio {
namespace alc5514 {

uint32_t Alc5514Device::ReadReg(uint32_t addr) {
    uint32_t buf = htobe32(addr);
    uint32_t val = 0;
    zx_status_t status = i2c_write_read_sync(&i2c_, &buf, sizeof(buf), &val, sizeof(val));
    if (status != ZX_OK) {
        zxlogf(ERROR, "alc5514: could not read reg addr: 0x%08x  status: %d\n", addr, status);
        return -1;
    }

    zxlogf(SPEW, "alc5514: register 0x%08x read 0x%08x\n", addr, betoh32(val));
    return betoh32(val);
}

void Alc5514Device::WriteReg(uint32_t addr, uint32_t val) {
    uint32_t buf[2];
    buf[0] = htobe32(addr);
    buf[1] = htobe32(val);
    zx_status_t status = i2c_write_sync(&i2c_, buf, sizeof(buf));
    if (status != ZX_OK) {
        zxlogf(ERROR, "alc5514: could not write reg addr/val: 0x%08x/0x%08x status: %d\n", addr,
               val, status);
    }

    zxlogf(SPEW, "alc5514: register 0x%08x write 0x%08x\n", addr, val);
}

void Alc5514Device::UpdateReg(uint32_t addr, uint32_t mask, uint32_t bits) {
    uint32_t val = ReadReg(addr);
    val = (val & ~mask) | bits;
    WriteReg(addr, val);
}

void Alc5514Device::DumpRegs() {
    uint32_t REGS[] = {
        PWR_ANA1,
        PWR_ANA2,
        I2S_CTRL1,
        I2S_CTRL2,
        DIG_IO_CTRL,
        PAD_CTRL1,
        DMIC_DATA_CTRL,
        DIG_SOURCE_CTRL,
        SRC_ENABLE,
        CLK_CTRL1,
        CLK_CTRL2,
        ASRC_IN_CTRL,
        DOWNFILTER0_CTRL1,
        DOWNFILTER0_CTRL2,
        DOWNFILTER0_CTRL3,
        DOWNFILTER1_CTRL1,
        DOWNFILTER1_CTRL2,
        DOWNFILTER1_CTRL3,
        ANA_CTRL_LDO10,
        ANA_CTRL_ADCFED,
        VERSION_ID,
        DEVICE_ID,
    };
    for (uint i = 0; i < fbl::count_of(REGS); i++) {
        zxlogf(INFO, "%04x: %08x\n", REGS[i], ReadReg(REGS[i]));
    }
}

zx_status_t Alc5514Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                    void* out_buf, size_t out_len, size_t* actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Alc5514Device::DdkUnbind() {
}

void Alc5514Device::DdkRelease() {
    delete this;
}

zx_status_t Alc5514Device::Initialize() {
    // The device can get confused if the I2C lines glitch together, as can happen
    // during bootup as regulators are turned off an on. If it's in this glitched
    // state the first i2c read will fail, so give it one chance to retry.
    uint32_t device = ReadReg(DEVICE_ID);
    if (device != DEVICE_ID_ALC5514) {
        device = ReadReg(DEVICE_ID);
    }
    if (device != DEVICE_ID_ALC5514) {
        zxlogf(INFO, "Device ID 0x%08x not supported\n", device);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Reset device
    WriteReg(RESET, RESET_VALUE);

    // GPIO4 = I2S_MCLK
    WriteReg(DIG_IO_CTRL, DIG_IO_CTRL_SEL_GPIO4_I2S_MCLK);
    // TDM_O_2 source PCM_DATA1_L/R
    // TDM_O_1 source PCM_DATA0_L/R
    UpdateReg(SRC_ENABLE, SRC_ENABLE_SRCOUT_1_INPUT_SEL_MASK | SRC_ENABLE_SRCOUT_2_INPUT_SEL_MASK,
                          SRC_ENABLE_SRCOUT_1_INPUT_SEL_PCM_DATA0_LR |
                          SRC_ENABLE_SRCOUT_2_INPUT_SEL_PCM_DATA1_LR);
    // Disable DLDO current limit control after power on
    UpdateReg(ANA_CTRL_LDO10, ANA_CTRL_LDO10_DLDO_I_LIMIT_EN, 0);
    // Unmute ADC front end L/R channel, set bias current = 3uA
    WriteReg(ANA_CTRL_ADCFED, ANA_CTRL_ADCFED_BIAS_CTRL_3UA);
    // Enable I2S ASRC clock (mystery bits)
    WriteReg(ASRC_IN_CTRL, 0x00000003);
    // Eliminate noise in ASRC case if the clock is asynchronous with LRCK (mystery bits)
    WriteReg(DOWNFILTER0_CTRL3, 0x10000362);
    WriteReg(DOWNFILTER1_CTRL3, 0x10000362);

    // Hardcode PCM config
    // TDM mode, 8x 16-bit slots, 4 channels, PCM-B
    WriteReg(I2S_CTRL1, I2S_CTRL1_MODE_SEL_TDM_MODE |
                        I2S_CTRL1_DATA_FORMAT_PCM_B |
                        I2S_CTRL1_TDMSLOT_SEL_RX_8CH |
                        I2S_CTRL1_TDMSLOT_SEL_TX_8CH);
    WriteReg(I2S_CTRL2, I2S_CTRL2_DOCKING_MODE_ENABLE |
                        I2S_CTRL2_DOCKING_MODE_4CH);

    // Set clk_sys_pre to I2S_MCLK
    // frequency is 24576000
    WriteReg(CLK_CTRL2, CLK_CTRL2_CLK_SYS_PRE_SEL_I2S_MCLK);

    // DMIC clock = /8
    // ADC1 clk = /3
    // clk_sys_div_out = /2
    // clk_adc_ana_256fs = /2
    UpdateReg(CLK_CTRL1, CLK_CTRL1_CLK_DMIC_OUT_SEL_MASK | CLK_CTRL1_CLK_AD_ANA1_SEL_MASK,
                         CLK_CTRL1_CLK_DMIC_OUT_SEL_DIV8 | CLK_CTRL1_CLK_AD_ANA1_SEL_DIV3);
    UpdateReg(CLK_CTRL2, CLK_CTRL2_CLK_SYS_DIV_OUT_MASK | CLK_CTRL2_SEL_ADC_OSR_MASK,
                         CLK_CTRL2_CLK_SYS_DIV_OUT_DIV2 | CLK_CTRL2_SEL_ADC_OSR_DIV2);

    // Gain value referenced from CrOS
    // Set ADC1/ADC2 capture gain to +23.6dB
    UpdateReg(DOWNFILTER0_CTRL1, DOWNFILTER_CTRL_AD_AD_GAIN_MASK, 0x6E);
    UpdateReg(DOWNFILTER0_CTRL2, DOWNFILTER_CTRL_AD_AD_GAIN_MASK, 0x6E);
    UpdateReg(DOWNFILTER1_CTRL1, DOWNFILTER_CTRL_AD_AD_GAIN_MASK, 0x6E);
    UpdateReg(DOWNFILTER1_CTRL2, DOWNFILTER_CTRL_AD_AD_GAIN_MASK, 0x6E);

    // Power up
    WriteReg(PWR_ANA1, PWR_ANA1_EN_SLEEP_RESET |
                       PWR_ANA1_DMIC_DATA_IN2 |
                       PWR_ANA1_POW_CKDET |
                       PWR_ANA1_POW_PLL |
                       PWR_ANA1_POW_LDO18_IN |
                       PWR_ANA1_POW_LDO18_ADC |
                       PWR_ANA1_POW_LDO21 |
                       PWR_ANA1_POW_BG_LDO18 |
                       PWR_ANA1_POW_BG_LDO21);
    WriteReg(PWR_ANA2, PWR_ANA2_POW_PLL2 |
                       PWR_ANA2_RSTB_PLL2 |
                       PWR_ANA2_POW_PLL2_LDO |
                       PWR_ANA2_POW_PLL1 |
                       PWR_ANA2_RSTB_PLL1 |
                       PWR_ANA2_POW_PLL1_LDO |
                       PWR_ANA2_POW_BG_MBIAS |
                       PWR_ANA2_POW_MBIAS |
                       PWR_ANA2_POW_VREF2 |
                       PWR_ANA2_POW_VREF1 |
                       PWR_ANA2_POWR_LDO16 |
                       PWR_ANA2_POWL_LDO16 |
                       PWR_ANA2_POW_ADC2 |
                       PWR_ANA2_POW_INPUT_BUF |
                       PWR_ANA2_POW_ADC1_R |
                       PWR_ANA2_POW_ADC1_L |
                       PWR_ANA2_POW2_BSTR |
                       PWR_ANA2_POW2_BSTL |
                       PWR_ANA2_POW_BSTR |
                       PWR_ANA2_POW_BSTL |
                       PWR_ANA2_POW_ADCFEDR |
                       PWR_ANA2_POW_ADCFEDL);

    // Enable DMIC1/2, ADC1, DownFilter0/1 clock
    uint32_t clk_enable = CLK_CTRL1_CLK_AD_ANA1_EN |
                          CLK_CTRL1_CLK_DMIC_OUT2_EN |
                          CLK_CTRL1_CLK_DMIC_OUT1_EN |
                          CLK_CTRL1_CLK_AD1_EN |
                          CLK_CTRL1_CLK_AD0_EN;
    UpdateReg(CLK_CTRL1, clk_enable, clk_enable);

    // Use tracking clock for DownFilter0/1
    UpdateReg(CLK_CTRL2, CLK_CTRL2_AD1_TRACK | CLK_CTRL2_AD0_TRACK,
                         CLK_CTRL2_AD1_TRACK | CLK_CTRL2_AD0_TRACK);

    // Enable path
    UpdateReg(DIG_SOURCE_CTRL,
              DIG_SOURCE_CTRL_AD1_INPUT_SEL_MASK | DIG_SOURCE_CTRL_AD0_INPUT_SEL_MASK,
              DIG_SOURCE_CTRL_AD0_INPUT_SEL_DMIC1 | DIG_SOURCE_CTRL_AD1_INPUT_SEL_DMIC2);

    // Unmute DMIC
    UpdateReg(DOWNFILTER0_CTRL1, DOWNFILTER_CTRL_AD_DMIC_MIX_MUTE, 0);
    UpdateReg(DOWNFILTER0_CTRL2, DOWNFILTER_CTRL_AD_DMIC_MIX_MUTE, 0);
    UpdateReg(DOWNFILTER1_CTRL1, DOWNFILTER_CTRL_AD_DMIC_MIX_MUTE, 0);
    UpdateReg(DOWNFILTER1_CTRL2, DOWNFILTER_CTRL_AD_DMIC_MIX_MUTE, 0);

    // Unmute ADC
    UpdateReg(DOWNFILTER0_CTRL1, DOWNFILTER_CTRL_AD_AD_MUTE, 0);
    UpdateReg(DOWNFILTER0_CTRL2, DOWNFILTER_CTRL_AD_AD_MUTE, 0);
    UpdateReg(DOWNFILTER1_CTRL1, DOWNFILTER_CTRL_AD_AD_MUTE, 0);
    UpdateReg(DOWNFILTER1_CTRL2, DOWNFILTER_CTRL_AD_AD_MUTE, 0);

    return ZX_OK;
}

zx_status_t Alc5514Device::Bind() {
    zx_status_t st = device_get_protocol(parent(), ZX_PROTOCOL_I2C, &i2c_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "alc5514: could not get I2C protocol: %d\n", st);
        return st;
    }

    st = Initialize();
    if (st != ZX_OK) {
        return st;
    }

    return DdkAdd("alc5514");
}

fbl::unique_ptr<Alc5514Device> Alc5514Device::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Alc5514Device> ret(new (&ac) Alc5514Device(parent));
    if (!ac.check()) {
        zxlogf(ERROR, "alc5514: out of memory attempting to allocate device\n");
        return nullptr;
    }
    return ret;
}
}  // namespace alc5514
}  // namespace audio

extern "C" {
zx_status_t alc5514_bind_hook(void* ctx, zx_device_t* parent) {
    auto dev = audio::alc5514::Alc5514Device::Create(parent);
    if (dev == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t st = dev->Bind();
    if (st == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        __UNUSED auto ptr = dev.release();
        return st;
    }

    return ZX_OK;
}
}  // extern "C"
