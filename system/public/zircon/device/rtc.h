// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <stdint.h>

__BEGIN_CDECLS;

typedef struct {
    uint8_t seconds, minutes, hours;
    uint8_t day, month;
    uint16_t year;
} rtc_t;

#define IOCTL_RTC_GET \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RTC, 0)

#define IOCTL_RTC_SET \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RTC, 1)

// ssize_t ioctl_rtc_get(int fd, rtc_t* rtc_out);
IOCTL_WRAPPER_OUT(ioctl_rtc_get, IOCTL_RTC_GET, rtc_t);

// ssize_t ioctl_rtc_set(int fd, const rtc_t* rtc_in);
IOCTL_WRAPPER_IN(ioctl_rtc_set, IOCTL_RTC_SET, rtc_t);

// A guest's RTC alarm, as the Nebula VMM keeps one per VM.
typedef struct {
    uint32_t vm_index;
    rtc_t alarm_time;
    uint8_t enabled;
    uint8_t pending;
} rtc_alarm_t;

// Read a VM's alarm: in, the VM index (int32_t); out, rtc_alarm_t.
#define IOCTL_RTC_GET_ALARM \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RTC, 2)

// Set a VM's alarm: in, rtc_alarm_t.
#define IOCTL_RTC_SET_ALARM \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RTC, 3)

// Get the event a VM's alarm signals: in, the VM index; out, an event.
#define IOCTL_RTC_GET_EVENT_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_RTC, 4)

// Signal a VM's alarm now: in, the VM index.
#define IOCTL_RTC_ALARM_VM \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RTC, 5)

// ssize_t ioctl_rtc_get_alarm(int fd, const int32_t* vmid, rtc_alarm_t* alarm_out);
IOCTL_WRAPPER_INOUT(ioctl_rtc_get_alarm, IOCTL_RTC_GET_ALARM, int32_t, rtc_alarm_t);

// ssize_t ioctl_rtc_set_alarm(int fd, const rtc_alarm_t* alarm_in);
IOCTL_WRAPPER_IN(ioctl_rtc_set_alarm, IOCTL_RTC_SET_ALARM, rtc_alarm_t);

// ssize_t ioctl_rtc_get_event_handle(int fd, const int32_t* vmid, zx_handle_t* out);
IOCTL_WRAPPER_INOUT(ioctl_rtc_get_event_handle, IOCTL_RTC_GET_EVENT_HANDLE, int32_t, zx_handle_t);

// ssize_t ioctl_rtc_alarm_vm(int fd, const int32_t* vmid);
IOCTL_WRAPPER_IN(ioctl_rtc_alarm_vm, IOCTL_RTC_ALARM_VM, int32_t);

__END_CDECLS;
