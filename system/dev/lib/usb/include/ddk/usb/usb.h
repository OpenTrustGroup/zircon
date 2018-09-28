// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-composite.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb.h>

__BEGIN_CDECLS;

// helper function for claiming additional interfaces that satisfy the want_interface predicate,
// want_interface will be passed the supplied arg
zx_status_t usb_claim_additional_interfaces(usb_composite_protocol_t* comp,
                                            bool (*want_interface)(usb_interface_descriptor_t*,
                                                                   void*),
                                            void* arg);

// Utilities for iterating through descriptors within a device's USB configuration descriptor
typedef struct {
    uint8_t* desc;      // start of configuration descriptor
    uint8_t* desc_end;  // end of configuration descriptor
    uint8_t* current;   // current position in configuration descriptor
} usb_desc_iter_t;

// initializes a usb_desc_iter_t
zx_status_t usb_desc_iter_init(usb_protocol_t* usb, usb_desc_iter_t* iter);

// releases resources in a usb_desc_iter_t
void usb_desc_iter_release(usb_desc_iter_t* iter);

// resets iterator to the beginning
void usb_desc_iter_reset(usb_desc_iter_t* iter);

// returns the next descriptor
usb_descriptor_header_t* usb_desc_iter_next(usb_desc_iter_t* iter);

// returns the next descriptor without incrementing the iterator
usb_descriptor_header_t* usb_desc_iter_peek(usb_desc_iter_t* iter);

// returns the next interface descriptor, optionally skipping alternate interfaces
usb_interface_descriptor_t* usb_desc_iter_next_interface(usb_desc_iter_t* iter, bool skip_alt);

// returns the next endpoint descriptor within the current interface
usb_endpoint_descriptor_t* usb_desc_iter_next_endpoint(usb_desc_iter_t* iter);

__END_CDECLS;
