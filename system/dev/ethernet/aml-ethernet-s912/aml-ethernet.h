// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/ethernet_board.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <fbl/atomic.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

namespace eth {

class AmlEthernet {
public:
    // GPIO Indexes.
    enum {
        PHY_RESET,
        PHY_INTR,
        GPIO_COUNT,
    };

    zx_device_t* device_;
    gpio_protocol_t gpios_[GPIO_COUNT];
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlEthernet);
    AmlEthernet(){};
    static zx_status_t Create(zx_device_t* device);

    // DDK Hooks.
    void DdkRelease(void* ctx);
    void DdkUnbind(void* ctx);
    void ReleaseBuffers();

    // ETH_BOARD protocol.
    static void ResetPhy(void* ctx);

private:
    // MMIO Indexes
    enum {
        MMIO_PERIPH,
        MMIO_HHI,
    };

    zx_status_t InitPdev(zx_device_t* parent);

    platform_device_protocol_t pdev_;

    i2c_protocol_t i2c_;

    io_buffer_t periph_regs_iobuff_;
    io_buffer_t hhi_regs_iobuff_;
};

} // namespace eth
