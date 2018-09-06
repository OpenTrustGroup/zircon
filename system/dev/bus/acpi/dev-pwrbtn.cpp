// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev.h"

#include <acpica/acpi.h>
#include <ddk/debug.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <hid/descriptor.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/device/input.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "errors.h"

class AcpiPwrbtnDevice;
using DeviceType = ddk::Device<AcpiPwrbtnDevice>;

class AcpiPwrbtnDevice : public DeviceType, public ddk::HidBusProtocol<AcpiPwrbtnDevice> {
public:
    static zx_status_t Create(zx_device_t* parent,
                              fbl::unique_ptr<AcpiPwrbtnDevice>* out);

    // hidbus protocol implementation
    zx_status_t HidBusQuery(uint32_t options, hid_info_t* info);
    zx_status_t HidBusStart(ddk::HidBusIfcProxy proxy);
    void HidBusStop();
    zx_status_t HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len);
    zx_status_t HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                size_t* out_len);
    zx_status_t HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len);
    zx_status_t HidBusGetIdle(uint8_t rpt_id, uint8_t* duration);
    zx_status_t HidBusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidBusGetProtocol(uint8_t* protocol);
    zx_status_t HidBusSetProtocol(uint8_t protocol);

    void DdkRelease();
    ~AcpiPwrbtnDevice();
private:
    explicit AcpiPwrbtnDevice(zx_device_t* parent);
    DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiPwrbtnDevice);

    static uint32_t FixedEventHandler(void* ctx);
    static void NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx);

    void HandlePress();
    void QueueHidReportLocked() TA_REQ(lock_);

    fbl::Mutex lock_;

    // Interface the driver is currently bound to
    ddk::HidBusIfcProxy proxy_;

    // Track the pressed state.  We don't receive up-events from ACPI, but we
    // may want to synthesize them in the future if we care about duration of
    // press.
    bool pressed_ TA_GUARDED(lock_) = false;

    static const uint8_t kHidDescriptor[];
    static const size_t kHidDescriptorLen;
    static constexpr size_t kHidReportLen = 1;
};

// We encode the power button as a System Power Down control in a System Control
// collection.
const uint8_t AcpiPwrbtnDevice::kHidDescriptor[] = {
    HID_USAGE_PAGE(0x01), // Usage Page (Generic Desktop)
    HID_USAGE(0x80), // Usage (System Control)

    HID_COLLECTION_APPLICATION,
    HID_USAGE(0x81), // Usage (System Power Down)
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_REPORT_COUNT(1),
    HID_REPORT_SIZE(1), // 1 bit for power-down
    HID_INPUT(0x06), // Input (Data,Var,Rel)
    HID_REPORT_SIZE(7), // 7 bits of padding
    HID_INPUT(0x03), // Input (Const,Var,Abs)
};

const size_t AcpiPwrbtnDevice::kHidDescriptorLen = sizeof(AcpiPwrbtnDevice::kHidDescriptor);

AcpiPwrbtnDevice::AcpiPwrbtnDevice(zx_device_t* parent)
    : DeviceType(parent) {
}

AcpiPwrbtnDevice::~AcpiPwrbtnDevice() {
    AcpiRemoveNotifyHandler(ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY | ACPI_DEVICE_NOTIFY,
                            NotifyHandler);
    AcpiRemoveFixedEventHandler(ACPI_EVENT_POWER_BUTTON, FixedEventHandler);
}

void AcpiPwrbtnDevice::HandlePress() {
    zxlogf(TRACE, "acpi-pwrbtn: pressed\n");

    fbl::AutoLock guard(&lock_);
    pressed_ = true;
    QueueHidReportLocked();
}

uint32_t AcpiPwrbtnDevice::FixedEventHandler(void* ctx) {
    auto dev = reinterpret_cast<AcpiPwrbtnDevice*>(ctx);

    dev->HandlePress();

    // Note that the spec indicates to return 0. The code in the
    // Intel implementation (AcpiEvFixedEventDetect) reads differently.
    return ACPI_INTERRUPT_HANDLED;
}

void AcpiPwrbtnDevice::NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx) {
    auto dev = reinterpret_cast<AcpiPwrbtnDevice*>(ctx);

    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS status = AcpiGetObjectInfo(handle, &info);
    if (status != AE_OK) {
        if (info) {
            ACPI_FREE(info);
        }
        return;
    }
    // Handle powerbutton events via the notify interface
    bool power_btn = false;
    if (info->Valid & ACPI_VALID_HID) {
        if (value == 128 &&
            !strncmp(info->HardwareId.String, "PNP0C0C", info->HardwareId.Length)) {

            power_btn = true;
        } else if (value == 199 &&
                   (!strncmp(info->HardwareId.String, "MSHW0028", info->HardwareId.Length) ||
                    !strncmp(info->HardwareId.String, "MSHW0040", info->HardwareId.Length))) {
            power_btn = true;
        }
    }

    if (power_btn) {
        dev->HandlePress();
    }

    ACPI_FREE(info);
}

void AcpiPwrbtnDevice::QueueHidReportLocked() {
    if (proxy_.is_valid()) {
        uint8_t report = 1;
        proxy_.IoQueue(&report, sizeof(report));
    }
}

zx_status_t AcpiPwrbtnDevice::HidBusQuery(uint32_t options, hid_info_t* info) {
    zxlogf(TRACE, "acpi-pwrbtn: hid bus query\n");

    info->dev_num = 0;
    info->dev_class = HID_DEV_CLASS_OTHER;
    info->boot_device = false;
    return ZX_OK;
}

zx_status_t AcpiPwrbtnDevice::HidBusStart(ddk::HidBusIfcProxy proxy) {
    zxlogf(TRACE, "acpi-pwrbtn: hid bus start\n");

    fbl::AutoLock guard(&lock_);
    if (proxy_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    }
    proxy_ = proxy;
    return ZX_OK;
}

void AcpiPwrbtnDevice::HidBusStop() {
    zxlogf(TRACE, "acpi-pwrbtn: hid bus stop\n");

    fbl::AutoLock guard(&lock_);
    proxy_.clear();
}

zx_status_t AcpiPwrbtnDevice::HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {
    zxlogf(TRACE, "acpi-pwrbtn: hid bus get descriptor\n");

    if (data == nullptr || len == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (desc_type != HID_DESC_TYPE_REPORT) {
        return ZX_ERR_NOT_FOUND;
    }

    *data = malloc(kHidDescriptorLen);
    if (*data == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    *len = kHidDescriptorLen;
    memcpy(*data, kHidDescriptor, kHidDescriptorLen);
    return ZX_OK;
}

zx_status_t AcpiPwrbtnDevice::HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                              size_t len, size_t* out_len) {
    if (out_len == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (rpt_type != HID_REPORT_TYPE_INPUT || rpt_id != 0) {
        return ZX_ERR_NOT_FOUND;
    }

    if (len < kHidReportLen) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    fbl::AutoLock guard(&lock_);
    uint8_t report = pressed_;
    static_assert(sizeof(report) == kHidReportLen, "");
    memcpy(data, &report, kHidReportLen);

    *out_len = kHidReportLen;
    return ZX_OK;
}

zx_status_t AcpiPwrbtnDevice::HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                            size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiPwrbtnDevice::HidBusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiPwrbtnDevice::HidBusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_OK;
}

zx_status_t AcpiPwrbtnDevice::HidBusGetProtocol(uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiPwrbtnDevice::HidBusSetProtocol(uint8_t protocol) {
    return ZX_OK;
}

void AcpiPwrbtnDevice::DdkRelease() {
    zxlogf(INFO, "acpi-pwrbtn: DdkRelease\n");
    delete this;
}

zx_status_t AcpiPwrbtnDevice::Create(zx_device_t* parent,
                                     fbl::unique_ptr<AcpiPwrbtnDevice>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<AcpiPwrbtnDevice> dev(new (&ac) AcpiPwrbtnDevice(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    ACPI_STATUS status = AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON,
                                                      FixedEventHandler,
                                                      dev.get());
    if (status != AE_OK) {
        // The dtor for AcpiPwrbtnDevice will clean these global handlers up when we
        // return here.
        return acpi_to_zx_status(status);
    }

    status = AcpiInstallNotifyHandler(ACPI_ROOT_OBJECT,
                                      ACPI_SYSTEM_NOTIFY | ACPI_DEVICE_NOTIFY,
                                      NotifyHandler,
                                      dev.get());
    if (status != AE_OK) {
        // The dtor for AcpiPwrbtnDevice will clean these global handlers up when we
        // return here.
        return acpi_to_zx_status(status);
    }

    *out = fbl::move(dev);
    return ZX_OK;
}

zx_status_t pwrbtn_init(zx_device_t* parent) {
    zxlogf(TRACE, "acpi-pwrbtn: init\n");

    fbl::unique_ptr<AcpiPwrbtnDevice> dev;
    zx_status_t status = AcpiPwrbtnDevice::Create(parent, &dev);
    if (status != ZX_OK) {
        return status;
    }

    status = dev->DdkAdd("acpi-pwrbtn");
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();

    zxlogf(INFO, "acpi-pwrbtn: initialized\n");
    return ZX_OK;
}
