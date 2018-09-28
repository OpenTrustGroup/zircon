// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/atomic.h>
#include <fbl/mutex.h>
#include <hid/gt92xx.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/types.h>

// clang-format off
#define GT_REG_SLEEP            0x8040
#define GT_REG_CONFIG_DATA      0x8047
#define GT_REG_MAX_X_LO         0x8048
#define GT_REG_MAX_X_HI         0x8049
#define GT_REG_MAX_Y_LO         0x804a
#define GT_REG_MAX_Y_HI         0x804b
#define GT_REG_NUM_FINGERS      0x804c

#define GT_REG_CONFIG_REFRESH   0x812a
#define GT_REG_VERSION          0x8140
#define GT_REG_SENSOR_ID        0x814a
#define GT_REG_TOUCH_STATUS     0x814e
#define GT_REG_REPORTS          0x814f

#define GT_REG_FIRMWARE         0x41e4
#define GT_FIRMWARE_MAGIC       0xbe
// clang-format on

namespace goodix {

class Gt92xxDevice : public ddk::Device<Gt92xxDevice, ddk::Unbindable>,
                     public ddk::HidBusProtocol<Gt92xxDevice> {
public:
    Gt92xxDevice(zx_device_t* device, ddk::I2cChannel i2c,
                 ddk::GpioPin intr, ddk::GpioPin reset)
        : ddk::Device<Gt92xxDevice, ddk::Unbindable>(device),
          i2c_(fbl::move(i2c)), int_gpio_(fbl::move(intr)),
          reset_gpio_(fbl::move(reset)){};

    static zx_status_t Create(zx_device_t* device);

    void DdkRelease();
    void DdkUnbind() __TA_EXCLUDES(proxy_lock_);

    // HidBus required methods
    void HidBusStop();
    zx_status_t HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len);
    zx_status_t HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                size_t len, size_t* out_len);
    zx_status_t HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                size_t len);
    zx_status_t HidBusGetIdle(uint8_t rpt_id, uint8_t* duration);
    zx_status_t HidBusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidBusGetProtocol(uint8_t* protocol);
    zx_status_t HidBusSetProtocol(uint8_t protocol);
    zx_status_t HidBusStart(ddk::HidBusIfcProxy proxy) __TA_EXCLUDES(proxy_lock_);
    zx_status_t HidBusQuery(uint32_t options, hid_info_t* info) __TA_EXCLUDES(proxy_lock_);

private:
    // Format of data as it is read from the device
    struct FingerReport {
        uint8_t id;
        uint16_t x;
        uint16_t y;
        uint16_t size;
        uint8_t reserved;
    } __PACKED;

    static constexpr uint32_t kMaxPoints = 5;

    zx_status_t ShutDown() __TA_EXCLUDES(proxy_lock_);
    // performs hardware reset using gpio
    void HWReset();
    zx_status_t Init();

    uint8_t Read(uint16_t addr);
    zx_status_t Read(uint16_t addr, uint8_t* buf, uint8_t len);
    zx_status_t Write(uint16_t addr, uint8_t val);

    int Thread();

    const ddk::I2cChannel i2c_;
    const ddk::GpioPin int_gpio_;
    const ddk::GpioPin reset_gpio_;

    gt92xx_touch_t gt_rpt_ __TA_GUARDED(proxy_lock_);
    zx::interrupt irq_;
    thrd_t thread_;
    fbl::atomic<bool> running_;
    fbl::Mutex proxy_lock_;
    ddk::HidBusIfcProxy proxy_ __TA_GUARDED(proxy_lock_);
};
}