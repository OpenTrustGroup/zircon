// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

zx_status_t usb_util_control(usb_device_t* dev, uint8_t request_type,  uint8_t request,
                             uint16_t value, uint16_t index, void* data, size_t length);

zx_status_t usb_util_get_descriptor(usb_device_t* dev, uint16_t type, uint16_t index,
                                    uint16_t language, void* data, size_t length);

// Fetch the descriptor using the provided descriptor ID and language ID.  If
// the language ID requested is not available, the first entry of the language
// ID table will be used instead and be provided in the updated version of the
// parameter.
//
// The string will be encoded using UTF-8, and will be truncated to fit the
// space provided by the buflen parameter.  buflen will be updated to indicate
// the amount of space needed to hold the actual UTF-8 encoded string lenth, and
// may be larger than the original value passed.  Embedded nulls may be present
// in the string, and the result may not be null terminated if the string
// occupies the entire provided buffer.
//
zx_status_t usb_util_get_string_descriptor(usb_device_t* dev, uint8_t desc_id,
                                           uint16_t* inout_lang_id, uint8_t* buf,
                                           size_t* inout_buflen);
