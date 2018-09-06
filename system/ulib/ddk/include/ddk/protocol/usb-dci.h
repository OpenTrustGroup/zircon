// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <ddk/protocol/usb.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/hw/usb.h>

__BEGIN_CDECLS;

// This protocol is used for USB peripheral controller drivers

// callbacks implemented by the USB device driver
typedef struct {
    // callback for handling ep0 control requests
    zx_status_t (*control)(void* ctx, const usb_setup_t* setup, void* buffer, size_t buffer_length,
                           size_t* out_actual);
    void (*set_connected)(void* ctx, bool connected);
    void (*set_speed)(void* ctx, usb_speed_t speed);
} usb_dci_interface_ops_t;

typedef struct {
    usb_dci_interface_ops_t* ops;
    void* ctx;
} usb_dci_interface_t;

static inline zx_status_t usb_dci_control(usb_dci_interface_t* intf, const usb_setup_t* setup,
                                          void* buffer, size_t buffer_length, size_t* out_actual) {
    return intf->ops->control(intf->ctx, setup, buffer, buffer_length, out_actual);
}

static inline void usb_dci_set_connected(usb_dci_interface_t* intf, bool connected) {
    intf->ops->set_connected(intf->ctx, connected);
}

static inline void usb_dci_set_speed(usb_dci_interface_t* intf, usb_speed_t speed) {
    intf->ops->set_speed(intf->ctx, speed);
}

typedef struct {
    void (*request_queue)(void* ctx, usb_request_t* req);
    zx_status_t (*set_interface)(void* ctx, usb_dci_interface_t* interface);
    zx_status_t (*config_ep)(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                             usb_ss_ep_comp_descriptor_t* ss_comp_desc);
    zx_status_t (*disable_ep)(void* ctx, uint8_t ep_addr);
    zx_status_t (*ep_set_stall)(void* ctx, uint8_t ep_address);
    zx_status_t (*ep_clear_stall)(void* ctx, uint8_t ep_address);
    zx_status_t (*get_bti)(void* ctx, zx_handle_t* out_handle);
} usb_dci_protocol_ops_t;

typedef struct {
    usb_dci_protocol_ops_t* ops;
    void* ctx;
} usb_dci_protocol_t;

static inline void usb_dci_request_queue(usb_dci_protocol_t* dci, usb_request_t* req) {
    dci->ops->request_queue(dci->ctx, req);
}

// registers callback interface with the controller driver
static inline void usb_dci_set_interface(usb_dci_protocol_t* dci, usb_dci_interface_t* intf) {
    dci->ops->set_interface(dci->ctx, intf);
}

static zx_status_t usb_dci_config_ep(usb_dci_protocol_t* dci,
                                     usb_endpoint_descriptor_t* ep_desc,
                                     usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return dci->ops->config_ep(dci->ctx, ep_desc, ss_comp_desc);
}

static inline zx_status_t usb_dci_disable_ep(usb_dci_protocol_t* dci, uint8_t ep_addr) {
    return dci->ops->disable_ep(dci->ctx, ep_addr);
}

static zx_status_t usb_dci_ep_set_stall(usb_dci_protocol_t* dci, uint8_t ep_address) {
    return dci->ops->ep_set_stall(dci->ctx, ep_address);
}

static zx_status_t usb_dci_ep_clear_stall(usb_dci_protocol_t* dci, uint8_t ep_address) {
    return dci->ops->ep_clear_stall(dci->ctx, ep_address);
}

// shares a copy of the DCI driver's BTI handle
static inline zx_status_t usb_dci_get_bti(usb_dci_protocol_t* dci, zx_handle_t* out_handle) {
    return dci->ops->get_bti(dci->ctx, out_handle);
}

__END_CDECLS;
