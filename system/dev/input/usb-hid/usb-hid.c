// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/hidbus.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb/usb.h>
#include <zircon/hw/usb-hid.h>

#include <zircon/status.h>
#include <zircon/types.h>
#include <pretty/hexdump.h>

#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define USB_HID_SUBCLASS_BOOT   0x01
#define USB_HID_PROTOCOL_KBD    0x01
#define USB_HID_PROTOCOL_MOUSE  0x02

#define to_usb_hid(d) containerof(d, usb_hid_device_t, hiddev)

typedef struct usb_hid_device {
    zx_device_t* zxdev;
    zx_device_t* usbdev;
    usb_protocol_t usb;

    hid_info_t info;
    usb_request_t* req;
    bool req_queued;

    mtx_t lock;
    hidbus_ifc_t* ifc;
    void* cookie;

    uint8_t interface;
    usb_desc_iter_t desc_iter;
    usb_hid_descriptor_t* hid_desc;
} usb_hid_device_t;

static void usb_interrupt_callback(usb_request_t* req, void* cookie) {
    usb_hid_device_t* hid = (usb_hid_device_t*)cookie;
    // TODO use usb request copyfrom instead of mmap
    void* buffer;
    zx_status_t status = usb_req_mmap(&hid->usb, req, &buffer);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb-hid: usb_req_mmap failed: %s\n", zx_status_get_string(status));
        return;
    }
    zxlogf(SPEW, "usb-hid: callback request status %d\n", req->response.status);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        hexdump(buffer, req->response.actual);
    }

    bool requeue = true;
    switch (req->response.status) {
    case ZX_ERR_IO_NOT_PRESENT:
        requeue = false;
        break;
    case ZX_OK:
        mtx_lock(&hid->lock);
        if (hid->ifc) {
            hid->ifc->io_queue(hid->cookie, buffer, req->response.actual);
        }
        mtx_unlock(&hid->lock);
        break;
    default:
        zxlogf(ERROR, "usb-hid: unknown interrupt status %d; not requeuing req\n",
                req->response.status);
        requeue = false;
        break;
    }

    if (requeue) {
        usb_request_queue(&hid->usb, req);
    } else {
        hid->req_queued = false;
    }
}

static zx_status_t usb_hid_query(void* ctx, uint32_t options, hid_info_t* info) {
    if (!info) {
        return ZX_ERR_INVALID_ARGS;
    }
    usb_hid_device_t* hid = ctx;
    info->dev_num = hid->info.dev_num;
    info->dev_class = hid->info.dev_class;
    info->boot_device = hid->info.boot_device;
    return ZX_OK;
}

static zx_status_t usb_hid_start(void* ctx, hidbus_ifc_t* ifc, void* cookie) {
    usb_hid_device_t* hid = ctx;
    mtx_lock(&hid->lock);
    if (hid->ifc) {
        mtx_unlock(&hid->lock);
        return ZX_ERR_ALREADY_BOUND;
    }
    hid->ifc = ifc;
    hid->cookie = cookie;
    if (!hid->req_queued) {
        hid->req_queued = true;
        usb_request_queue(&hid->usb, hid->req);
    }
    mtx_unlock(&hid->lock);
    return ZX_OK;
}

static void usb_hid_stop(void* ctx) {
    // TODO(tkilbourn) set flag to stop requeueing the interrupt request when we start using
    // this callback
    usb_hid_device_t* hid = ctx;
    mtx_lock(&hid->lock);
    hid->ifc = NULL;
    hid->cookie = NULL;
    mtx_unlock(&hid->lock);
}

static zx_status_t usb_hid_control(usb_hid_device_t* hid, uint8_t req_type, uint8_t request,
                                   uint16_t value, uint16_t index, void* data, size_t length,
                                   size_t* out_length) {
    zx_status_t status = usb_control(&hid->usb, req_type, request, value, index, data, length,
                                     ZX_TIME_INFINITE, out_length);
    if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
        usb_reset_endpoint(&hid->usb, 0);
    }
    return status;
}

static zx_status_t usb_hid_get_descriptor(void* ctx, uint8_t desc_type,
                                          void** data, size_t* len) {
    usb_hid_device_t* hid = ctx;
    int desc_idx = -1;
    for (int i = 0; i < hid->hid_desc->bNumDescriptors; i++) {
        if (hid->hid_desc->descriptors[i].bDescriptorType == desc_type) {
            desc_idx = i;
            break;
        }
    }
    if (desc_idx < 0) {
        return ZX_ERR_NOT_FOUND;
    }

    size_t desc_len = hid->hid_desc->descriptors[desc_idx].wDescriptorLength;
    uint8_t* desc_buf = malloc(desc_len);
    zx_status_t status = usb_hid_control(hid, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
                                         USB_REQ_GET_DESCRIPTOR, desc_type << 8,
                                         hid->interface, desc_buf, desc_len, len);
    if (status < 0) {
        zxlogf(ERROR, "usb-hid: error reading report descriptor 0x%02x: %d\n", desc_type, status);
        free(desc_buf);
        return status;
    } else {
        *data = desc_buf;
    }
    return ZX_OK;
}

static zx_status_t usb_hid_get_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                      void* data, size_t len, size_t* out_len) {
    if (out_len == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    usb_hid_device_t* hid = ctx;
    return usb_hid_control(hid, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_HID_GET_REPORT, (rpt_type << 8 | rpt_id),
                           hid->interface, data, len, out_len);
}

static zx_status_t usb_hid_set_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                      void* data, size_t len) {
    usb_hid_device_t* hid = ctx;
    return usb_hid_control(hid, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_HID_SET_REPORT, (rpt_type << 8 | rpt_id),
                           hid->interface, data, len, NULL);
}

static zx_status_t usb_hid_get_idle(void* ctx, uint8_t rpt_id, uint8_t* duration) {
    usb_hid_device_t* hid = ctx;
    return usb_hid_control(hid, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_HID_GET_IDLE, rpt_id, hid->interface,
                           duration, sizeof(*duration), NULL);
}

static zx_status_t usb_hid_set_idle(void* ctx, uint8_t rpt_id, uint8_t duration) {
    usb_hid_device_t* hid = ctx;
    return usb_hid_control(hid, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_HID_SET_IDLE, (duration << 8) | rpt_id,
                           hid->interface, NULL, 0, NULL);
}

static zx_status_t usb_hid_get_protocol(void* ctx, uint8_t* protocol) {
    usb_hid_device_t* hid = ctx;
    return usb_hid_control(hid, USB_DIR_IN, USB_HID_GET_PROTOCOL, 0, hid->interface, protocol,
                           sizeof(*protocol), NULL);
}

static zx_status_t usb_hid_set_protocol(void* ctx, uint8_t protocol) {
    usb_hid_device_t* hid = ctx;
    return usb_hid_control(hid, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                           USB_HID_SET_PROTOCOL, protocol, hid->interface, NULL,
                           0, NULL);
}

static hidbus_protocol_ops_t usb_hid_bus_ops = {
    .query = usb_hid_query,
    .start = usb_hid_start,
    .stop = usb_hid_stop,
    .get_descriptor = usb_hid_get_descriptor,
    .get_report = usb_hid_get_report,
    .set_report = usb_hid_set_report,
    .get_idle = usb_hid_get_idle,
    .set_idle = usb_hid_set_idle,
    .get_protocol = usb_hid_get_protocol,
    .set_protocol = usb_hid_set_protocol,
};

static void usb_hid_unbind(void* ctx) {
    usb_hid_device_t* hid = ctx;
    device_remove(hid->zxdev);
}

static void usb_hid_release(void* ctx) {
    usb_hid_device_t* hid = ctx;
    usb_req_release(&hid->usb, hid->req);
    usb_desc_iter_release(&hid->desc_iter);
    free(hid);
}

static zx_protocol_device_t usb_hid_dev_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_hid_unbind,
    .release = usb_hid_release,
};

static zx_status_t usb_hid_bind(void* ctx, zx_device_t* dev) {
    usb_hid_device_t* usbhid = calloc(1, sizeof(usb_hid_device_t));
    if (usbhid == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(dev, ZX_PROTOCOL_USB, &usbhid->usb);
    if (status != ZX_OK) {
        goto fail;
    }

    status = usb_desc_iter_init(&usbhid->usb, &usbhid->desc_iter);
    if (status != ZX_OK) {
        goto fail;
    }

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&usbhid->desc_iter, true);
     if (!intf || intf->bInterfaceClass != USB_CLASS_HID) {
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    usb_endpoint_descriptor_t* endpt = NULL;
    usb_hid_descriptor_t* hid_desc = NULL;

    // look for interrupt endpoint and HID descriptor
    usb_descriptor_header_t* header = usb_desc_iter_next(&usbhid->desc_iter);
    while (header && !(endpt && hid_desc)) {
        if (header->bDescriptorType == USB_DT_HID) {
            hid_desc = (usb_hid_descriptor_t *)header;
        } else if (header->bDescriptorType == USB_DT_ENDPOINT) {
            endpt = (usb_endpoint_descriptor_t *)header;
            if (usb_ep_direction(endpt) != USB_ENDPOINT_IN ||
                usb_ep_type(endpt) != USB_ENDPOINT_INTERRUPT) {
                endpt = NULL;
            }
        }
        header = usb_desc_iter_next(&usbhid->desc_iter);
    }

    if (!endpt || !hid_desc) {
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    usbhid->usbdev = dev;
    usbhid->interface = usbhid->info.dev_num = intf->bInterfaceNumber;
    usbhid->hid_desc = hid_desc;

    usbhid->info.boot_device = intf->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT;
    usbhid->info.dev_class = HID_DEV_CLASS_OTHER;
    if (intf->bInterfaceProtocol == USB_HID_PROTOCOL_KBD) {
        usbhid->info.dev_class = HID_DEV_CLASS_KBD;
    } else if (intf->bInterfaceProtocol == USB_HID_PROTOCOL_MOUSE) {
        usbhid->info.dev_class = HID_DEV_CLASS_POINTER;
    }

    status = usb_req_alloc(&usbhid->usb, &usbhid->req, usb_ep_max_packet(endpt),
                               endpt->bEndpointAddress);
    if (status != ZX_OK) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    usbhid->req->complete_cb = usb_interrupt_callback;
    usbhid->req->cookie = usbhid;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-hid",
        .ctx = usbhid,
        .ops = &usb_hid_dev_ops,
        .proto_id = ZX_PROTOCOL_HIDBUS,
        .proto_ops = &usb_hid_bus_ops,
    };

    status = device_add(dev, &args, &usbhid->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;

fail:
    if (usbhid->req) {
        usb_req_release(&usbhid->usb, usbhid->req);
    }
    usb_desc_iter_release(&usbhid->desc_iter);
    free(usbhid);
    return status;
}

static zx_driver_ops_t usb_hid_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_hid_bind,
};

ZIRCON_DRIVER_BEGIN(usb_hid, usb_hid_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HID),
ZIRCON_DRIVER_END(usb_hid)
