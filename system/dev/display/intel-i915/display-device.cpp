// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <float.h>
#include <math.h>
#include <zircon/device/backlight.h>

#include "display-device.h"
#include "intel-i915.h"
#include "macros.h"
#include "registers.h"
#include "registers-dpll.h"
#include "registers-transcoder.h"
#include "tiling.h"

namespace {

zx_status_t backlight_ioctl(void* ctx, uint32_t op,
                            const void* in_buf, size_t in_len,
                            void* out_buf, size_t out_len, size_t* out_actual) {
    if (op == IOCTL_BACKLIGHT_SET_BRIGHTNESS
            || op == IOCTL_BACKLIGHT_GET_BRIGHTNESS) {
        auto ref = static_cast<i915::display_ref_t*>(ctx);
        fbl::AutoLock lock(&ref->mtx);

        if (ref->display_device == NULL) {
            return ZX_ERR_PEER_CLOSED;
        }

        if (op == IOCTL_BACKLIGHT_SET_BRIGHTNESS) {
            if (in_len != sizeof(backlight_state_t) || out_len != 0) {
                return ZX_ERR_INVALID_ARGS;
            }

            const auto args = static_cast<const backlight_state_t*>(in_buf);
            ref->display_device->SetBacklightState(args->on, args->brightness);
            return ZX_OK;
        } else if (op == IOCTL_BACKLIGHT_GET_BRIGHTNESS) {
            if (out_len != sizeof(backlight_state_t) || in_len != 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            
            auto args = static_cast<backlight_state_t*>(out_buf);
            ref->display_device->GetBacklightState(&args->on, &args->brightness);
            *out_actual = sizeof(backlight_state_t);
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_SUPPORTED;
}

void backlight_release(void* ctx) {
    delete static_cast<i915::display_ref_t*>(ctx);
}

static zx_protocol_device_t backlight_ops = {};

uint32_t float_to_i915_csc_offset(float f) {
    ZX_DEBUG_ASSERT(0 <= f && f < 1.0f); // Controller::CheckConfiguration validates this

    // f is in [0, 1). Multiply by 2^12 to convert to a 12-bit fixed-point fraction.
    return static_cast<uint32_t>(f * pow(FLT_RADIX, 12));
}

uint32_t float_to_i915_csc_coefficient(float f) {
    registers::CscCoeffFormat res;
    if (f < 0) {
        f *= -1;
        res.set_sign(1);
    }

    if (f < .125) {
        res.set_exponent(res.kExponent0125);
        f /= .125f;
    } else if (f < .25) {
        res.set_exponent(res.kExponent025);
        f /= .25f;
    } else if (f < .5) {
        res.set_exponent(res.kExponent05);
        f /= .5f;
    } else if (f < 1) {
        res.set_exponent(res.kExponent1);
    } else if (f < 2) {
        res.set_exponent(res.kExponent2);
        f /= 2.0f;
    } else {
        res.set_exponent(res.kExponent4);
        f /= 4.0f;
    }
    f = (f * 512) + .5f;

    if (f >= 512) {
        res.set_mantissa(0x1ff);
    } else {
        res.set_mantissa(static_cast<uint16_t>(f));
    }

    return res.reg_value();
}

uint32_t encode_pipe_color_component(uint8_t component) {
    // Convert to unsigned .10 fixed point format
    return component << 2;
}

} // namespace

namespace i915 {

DisplayDevice::DisplayDevice(Controller* controller, uint64_t id, registers::Ddi ddi)
        : controller_(controller), id_(id), ddi_(ddi) {}

DisplayDevice::~DisplayDevice() {
    if (pipe_) {
        pipe_->Reset();
        pipe_->Detach();
    }
    if (inited_) {
        controller_->ResetDdi(ddi());
    }
    if (display_ref_) {
        fbl::AutoLock lock(&display_ref_->mtx);
        device_remove(backlight_device_);
        display_ref_->display_device = nullptr;
    }
}

hwreg::RegisterIo* DisplayDevice::mmio_space() const {
    return controller_->mmio_space();
}

bool DisplayDevice::Init() {
    ddi_power_ = controller_->power()->GetDdiPowerWellRef(ddi_);

    if (!InitDdi()) {
        return false;
    }

    inited_ = true;

    InitBacklight();

    return true;
}

void DisplayDevice::InitBacklight() {
    if (HasBacklight() && InitBacklightHw()) {
        fbl::AllocChecker ac;
        auto display_ref = fbl::make_unique_checked<display_ref_t>(&ac);
        zx_status_t status = ZX_ERR_NO_MEMORY;
        if (ac.check()) {
            mtx_init(&display_ref->mtx, mtx_plain);
            {
                fbl::AutoLock lock(&display_ref->mtx);
                display_ref->display_device = this;
            }

            backlight_ops.version = DEVICE_OPS_VERSION;
            backlight_ops.ioctl = backlight_ioctl;
            backlight_ops.release = backlight_release;

            device_add_args_t args = {};
            args.version = DEVICE_ADD_ARGS_VERSION;
            args.name = "backlight";
            args.ctx = display_ref.get();
            args.ops = &backlight_ops;
            args.proto_id = ZX_PROTOCOL_BACKLIGHT;

            if ((status = device_add(controller_->zxdev(), &args, &backlight_device_)) == ZX_OK) {
                display_ref_ = display_ref.release();
            }
        }
        if (display_ref_ == nullptr) {
            LOG_WARN("Failed to add backlight (%d)\n", status);
        }

        SetBacklightState(true, 255);
    }
}

bool DisplayDevice::Resume() {
    if (!DdiModeset(info_, pipe_->pipe(), pipe_->transcoder())) {
        return false;
    }
    if (pipe_) {
        pipe_->Resume();
    }
    return true;
}

void DisplayDevice::LoadActiveMode() {
    pipe_->LoadActiveMode(&info_);
    info_.pixel_clock_10khz = LoadClockRateForTranscoder(pipe_->transcoder());
}

bool DisplayDevice::AttachPipe(Pipe* pipe) {
    if (pipe == pipe_) {
        return false;
    }

    if (pipe_) {
        pipe_->Reset();
        pipe_->Detach();
    }
    if (pipe) {
        pipe->AttachToDisplay(id_, controller()->igd_opregion().IsEdp(ddi()));

        if (info_.h_addressable) {
            PipeConfigPreamble(info_, pipe->pipe(), pipe->transcoder());
            pipe->ApplyModeConfig(info_);
            PipeConfigEpilogue(info_, pipe->pipe(), pipe->transcoder());
        }
    }
    pipe_ = pipe;
    return true;
}

bool DisplayDevice::CheckNeedsModeset(const display_mode_t* mode) {
    // Check the clock and the flags later
    size_t cmp_start = offsetof(display_mode_t, h_addressable);
    size_t cmp_end = offsetof(display_mode_t, flags);
    if (memcmp(&mode->h_addressable, &info_.h_addressable, cmp_end - cmp_start)) {
        // Modeset is necessary if display params other than the clock frequency differ
        LOG_SPEW("Modeset necessary for display params");
        return true;
    }

    // TODO(stevensd): There are still some situations where the BIOS is better at setting up
    // the display than we are. The BIOS seems to not always set the hsync/vsync polarity, so
    // don't include that in the check for already initialized displays. Once we're better at
    // initializing displays, merge the flags check back into the above memcmp.
    if ((mode->flags & MODE_FLAG_INTERLACED) != (info_.flags & MODE_FLAG_INTERLACED)) {
        LOG_SPEW("Modeset necessary for display flags");
        return true;
    }

    if (mode->pixel_clock_10khz == info_.pixel_clock_10khz) {
        // Modeset is necessary not necessary if all display params are the same
        return false;
    }

    // Check to see if the hardware was already configured properly. The is primarily to
    // prevent unnecessary modesetting at startup. The extra work this adds to regular
    // modesetting is negligible.
    auto dpll_ctrl2 = registers::DpllControl2::Get().ReadFrom(mmio_space());
    const dpll_state_t* current_state = nullptr;
    if (!dpll_ctrl2.ddi_clock_off(ddi()).get()) {
        current_state = controller_->GetDpllState(
                static_cast<registers::Dpll>(dpll_ctrl2.ddi_clock_select(ddi()).get()));
    }

    if (current_state == nullptr) {
        LOG_SPEW("Modeset necessary for clock");
        return true;
    }

    dpll_state_t new_state;
    if (!ComputeDpllState(mode->pixel_clock_10khz, &new_state)) {
        // ComputeDpllState should be validated in the display's CheckDisplayMode
        ZX_ASSERT(false);
    }

    // Modesetting is necessary if the states are not equal
    bool res = !Controller::CompareDpllStates(*current_state, new_state);
    if (res) {
        LOG_SPEW("Modeset necessary for clock state");
    }
    return res;
}

void DisplayDevice::ApplyConfiguration(const display_config_t* config) {
    ZX_ASSERT(config);

    if (CheckNeedsModeset(&config->mode)) {
        info_ = config->mode;

        DdiModeset(info_, pipe_->pipe(), pipe_->transcoder());

        PipeConfigPreamble(info_, pipe_->pipe(), pipe_->transcoder());
        pipe_->ApplyModeConfig(info_);
        PipeConfigEpilogue(info_, pipe_->pipe(), pipe_->transcoder());
    }

    pipe_->ApplyConfiguration(config);
}

} // namespace i915
