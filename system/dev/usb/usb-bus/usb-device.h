// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/usb-hci.h>
#include <zircon/hw/usb.h>
#include <ddk/protocol/usb-hub.h>

#include <threads.h>

typedef enum {
    // The interface has not been claimed and no device has been created for it.
    AVAILABLE,
    // Another interface has claimed the interface.
    CLAIMED,
    // A child device has been created for the interface.
    CHILD_DEVICE
} interface_status_t;

typedef struct usb_bus usb_bus_t;

// Represents a USB top-level device
typedef struct usb_device {
    zx_device_t* zxdev;
    zx_device_t* hci_zxdev;
    usb_hci_protocol_t hci;
    usb_bus_t* bus;

    // ID assigned by host controller
    uint32_t device_id;
    // device_id of the hub we are attached to (or zero for root hub)
    uint32_t hub_id;
    usb_speed_t speed;

    // true if device is a hub
    bool isHub;
    // Interface to talk to the hub driver
    usb_hub_interface_t hub_intf;

    usb_device_descriptor_t device_desc;
    usb_configuration_descriptor_t** config_descs;
    int current_config_index;

    mtx_t interface_mutex;
    // Array storing whether interfaces from 0 to bNumInterfaces-1
    // are available, claimed or is a child device.
    interface_status_t* interface_statuses;
    // list of child devices (for USB composite devices)
    list_node_t children;

    list_node_t node;

    // pool of requests that can be reused
    usb_request_pool_t free_reqs;
} usb_device_t;

zx_status_t usb_device_add(usb_bus_t* bus, uint32_t device_id, uint32_t hub_id,
                           usb_speed_t speed, usb_device_t** out_device);

void usb_device_remove(usb_device_t* dev);
void usb_device_set_hub_interface(usb_device_t* dev, usb_hub_interface_t* hub_intf);

zx_status_t usb_device_set_interface(usb_device_t* device, uint8_t interface_id,
                                     uint8_t alt_setting);

// Marks the interface as claimed, removing the device if it exists.
// Returns an error if the interface was already claimed by another interface.
zx_status_t usb_device_claim_interface(usb_device_t* dev, uint8_t interface_id);

zx_status_t usb_device_set_configuration(usb_device_t* dev, int config);
