// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/display-controller.h>
#include <ddktl/device.h>
#include <hwreg/mmio.h>
#include <region-alloc/region-alloc.h>
#include <lib/zx/vmo.h>

#include "gtt.h"
#include "pipe.h"
#include "power.h"
#include "registers-ddi.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

namespace i915 {

class Controller;
class DisplayDevice;

// Thread safe weak-ref to the DisplayDevice, because the backlight device
// lifecycle is managed by devmgr but the DisplayDevice lifecycle is managed
// by the display controller class.
typedef struct display_ref {
    mtx_t mtx;
    DisplayDevice* display_device __TA_GUARDED(mtx);
} display_ref_t;


class DisplayDevice {
public:
    DisplayDevice(Controller* device, uint64_t id, registers::Ddi ddi);
    virtual ~DisplayDevice();

    bool AttachPipe(Pipe* pipe);
    void ApplyConfiguration(const display_config_t* config);

    // Query whether or not there is a display attached to this ddi. Does not
    // actually do any initialization - that is done by Init.
    virtual bool Query() = 0;
    // Does display mode agnostic ddi initialization - subclasses implement InitDdi.
    bool Init();
    // Resumes the ddi after suspend.
    bool Resume();
    // Loads ddi state from the hardware at driver startup.
    void LoadActiveMode();
    // Method to allow the display device to handle hotplug events. Returns
    // true if the device can handle the event without disconnecting. Otherwise
    // the device will be removed.
    virtual bool HandleHotplug(bool long_pulse) { return false; }

    uint64_t id() const { return id_; }
    registers::Ddi ddi() const { return ddi_; }
    Controller* controller() { return controller_; }

    virtual uint32_t i2c_bus_id() const = 0;

    Pipe* pipe() const { return pipe_; }

    bool is_hdmi() const { return is_hdmi_; }
    void set_is_hdmi(bool is_hdmi) { is_hdmi_ = is_hdmi; }

    virtual bool HasBacklight() { return false; }
    virtual void SetBacklightState(bool power, uint8_t brightness) {}
    virtual void GetBacklightState(bool* power, uint8_t* brightness) {}

    virtual bool CheckPixelRate(uint64_t pixel_rate) = 0;
protected:
    // Attempts to initialize the ddi.
    virtual bool InitDdi() = 0;

    // Configures the hardware to display content at the given resolution.
    virtual bool DdiModeset(const display_mode_t& mode,
                            registers::Pipe pipe, registers::Trans trans) = 0;
    virtual bool ComputeDpllState(uint32_t pixel_clock_10khz, struct dpll_state* config) = 0;

    // Attaching a pipe to a display or configuring a pipe after display mode change has
    // 3 steps. The second step is generic pipe configuration, whereas PipeConfigPreamble
    // and PipeConfigEpilogue are responsible for display-type-specific configuration that
    // must be done before and after the generic configuration.
    virtual bool PipeConfigPreamble(const display_mode_t& mode,
                                    registers::Pipe pipe, registers::Trans trans) = 0;
    virtual bool PipeConfigEpilogue(const display_mode_t& mode,
                                    registers::Pipe pipe, registers::Trans trans) = 0;

    hwreg::RegisterIo* mmio_space() const;

private:
    bool CheckNeedsModeset(const display_mode_t* mode);

    // Borrowed reference to Controller instance
    Controller* controller_;

    uint64_t id_;
    registers::Ddi ddi_;

    Pipe* pipe_= nullptr;

    PowerWellRef ddi_power_;

    bool inited_ = false;
    display_mode_t info_ = {};
    bool is_hdmi_ = false;

    zx_device_t* backlight_device_ = nullptr;
    display_ref_t* display_ref_ = nullptr;
};

} // namespace i915
