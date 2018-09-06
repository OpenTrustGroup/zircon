// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include <ddk/device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-device.h>
#include <dev/pci/designware/atu-cfg.h>
#include <fbl/unique_ptr.h>

#include "aml-pcie.h"

namespace pcie {
namespace aml {

class AmlPcieDevice {
  public:
    AmlPcieDevice(zx_device_t* parent) : parent_(parent) {}
    ~AmlPcieDevice();

    zx_status_t Init();

  private:
    zx_status_t InitProtocols();
    zx_status_t InitMmios();
    zx_status_t InitMetadata();

    zx_device_t* parent_;
    zx_device_t* dev_;

    // Protocols
    platform_device_protocol_t pdev_;
    clk_protocol_t clk_;
    gpio_protocol_t gpio_;

    // IO Buffers
    io_buffer_t dbi_;
    io_buffer_t cfg_;
    io_buffer_t rst_;
    io_buffer_t pll_;

    // Device Metadata
    iatu_translation_entry_t atu_cfg_;
    iatu_translation_entry_t atu_io_;
    iatu_translation_entry_t atu_mem_;

    fbl::unique_ptr<AmlPcie> pcie_;
};

} // namespace aml
} // namespace pcie