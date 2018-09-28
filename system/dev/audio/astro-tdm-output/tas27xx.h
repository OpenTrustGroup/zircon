// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>
#include <ddktl/pdev.h>
#include <fbl/unique_ptr.h>



namespace audio {
namespace astro {

static constexpr uint8_t SW_RESET = 0x01;       //sw reset
static constexpr uint8_t PWR_CTL = 0x02;        //power control
static constexpr uint8_t PB_CFG2 = 0x05;        //pcm gain register
static constexpr uint8_t TDM_CFG0 = 0x0a;
static constexpr uint8_t TDM_CFG1 = 0x0b;
static constexpr uint8_t TDM_CFG2 = 0x0c;
static constexpr uint8_t TDM_CFG3 = 0x0d;
static constexpr uint8_t TDM_CFG4 = 0x0e;
static constexpr uint8_t TDM_CFG5 = 0x0f;
static constexpr uint8_t TDM_CFG6 = 0x10;
static constexpr uint8_t TDM_CFG7 = 0x11;
static constexpr uint8_t TDM_CFG8 = 0x12;
static constexpr uint8_t TDM_CFG9 = 0x13;
static constexpr uint8_t TDM_CFG10 = 0x14;
static constexpr uint8_t CLOCK_CFG = 0x3c;      //Clock Config

class Tas27xx : public fbl::unique_ptr<Tas27xx> {
public:
    static fbl::unique_ptr<Tas27xx> Create(ddk::I2cChannel&& i2c);
    bool ValidGain(float gain);
    zx_status_t SetGain(float gain);
    float GetGain() const { return current_gain_; }
    float GetMinGain() const { return kMinGain; }
    float GetMaxGain() const { return kMaxGain; }
    float GetGainStep() const { return kGainStep; }

    zx_status_t Init();
    zx_status_t Reset();
    zx_status_t Standby();
    zx_status_t ExitStandby();

private:
    friend class fbl::unique_ptr<Tas27xx>;
    static constexpr float kMaxGain = 0;
    static constexpr float kMinGain = -100.0;
    static constexpr float kGainStep = 0.5;

    Tas27xx() = default;
    ~Tas27xx() = default;

    zx_status_t WriteReg(uint8_t reg, uint8_t value);
    uint8_t ReadReg(uint8_t reg);

    zx_status_t SetStandby(bool stdby);

    ddk::I2cChannel i2c_;

    float current_gain_ = 0;
};
} // namespace astro
} // namespace audio