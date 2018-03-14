// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/usb-request.h>
#include <driver/usb.h>
#include <zircon/hw/usb-cdc.h>
#include <sync/completion.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define CDC_SUPPORTED_VERSION 0x0110 /* 1.10 */

// The maximum amount of memory we are willing to allocate to transaction buffers
#define MAX_TX_BUF_SZ 32768
#define MAX_RX_BUF_SZ 32768

const char* module_name = "usb-cdc-ecm";

typedef struct {
    uint8_t addr;
    uint16_t max_packet_size;
} ecm_endpoint_t;

typedef struct {
    zx_device_t* zxdev;
    zx_device_t* usb_device;
    usb_protocol_t usb;

    mtx_t ethmac_mutex;
    ethmac_ifc_t* ethmac_ifc;
    void* ethmac_cookie;

    uint64_t ticks_per_second;

    // Device attributes
    uint8_t mac_addr[ETH_MAC_SIZE];
    uint16_t mtu;

    // Connection attributes
    bool online;
    uint32_t ds_bps;
    uint32_t us_bps;

    // Interrupt handling
    ecm_endpoint_t int_endpoint;
    usb_request_t* int_txn_buf;
    completion_t completion;
    thrd_t int_thread;

    // Send context
    mtx_t tx_mutex;
    ecm_endpoint_t tx_endpoint;
    list_node_t tx_txn_bufs;        // list of usb_request_t
    list_node_t tx_pending_infos;   // list of ethmac_netbuf_t
    uint64_t tx_drop_notice_ticks;
    bool unbound;                   // set to true when device is going away. Guarded by tx_mutex

    // Receive context
    ecm_endpoint_t rx_endpoint;

} ecm_ctx_t;

static void ecm_unbind(void* cookie) {
    zxlogf(TRACE, "%s: unbinding\n", module_name);
    ecm_ctx_t* ctx = cookie;

    mtx_lock(&ctx->tx_mutex);
    ctx->unbound = true;
    if (ctx->ethmac_ifc) {
        ethmac_netbuf_t* netbuf;
        while ((netbuf = list_remove_head_type(&ctx->tx_pending_infos, ethmac_netbuf_t, node)) !=
               NULL) {
            ctx->ethmac_ifc->complete_tx(ctx->ethmac_cookie, netbuf, ZX_ERR_PEER_CLOSED);
        }
    }
    mtx_unlock(&ctx->tx_mutex);

    device_remove(ctx->zxdev);
}

static void ecm_free(ecm_ctx_t* ctx) {
    zxlogf(TRACE, "%s: deallocating memory\n", module_name);
    if (ctx->int_thread) {
        thrd_join(ctx->int_thread, NULL);
    }
    usb_request_t* txn;
    while ((txn = list_remove_head_type(&ctx->tx_txn_bufs, usb_request_t, node)) != NULL) {
        usb_request_release(txn);
    }
    usb_request_release(ctx->int_txn_buf);
    mtx_destroy(&ctx->ethmac_mutex);
    mtx_destroy(&ctx->tx_mutex);
    free(ctx);
}

static void ecm_release(void* ctx) {
    ecm_ctx_t* eth = ctx;
    ecm_free(eth);
}

static zx_protocol_device_t ecm_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = ecm_unbind,
    .release = ecm_release,
};

static void ecm_update_online_status(ecm_ctx_t* ctx, bool is_online) {
    mtx_lock(&ctx->ethmac_mutex);
    if ((is_online && ctx->online) || (!is_online && !ctx->online)) {
        goto done;
    }

    if (is_online) {
        zxlogf(INFO, "%s: connected to network\n", module_name);
        ctx->online = true;
        if (ctx->ethmac_ifc) {
            ctx->ethmac_ifc->status(ctx->ethmac_cookie, ETH_STATUS_ONLINE);
        } else {
            zxlogf(ERROR, "%s: not connected to ethermac interface\n", module_name);
        }
    } else {
        zxlogf(INFO, "%s: no connection to network\n", module_name);
        ctx->online = false;
        if (ctx->ethmac_ifc) {
            ctx->ethmac_ifc->status(ctx->ethmac_cookie, 0);
        }
    }

done:
    mtx_unlock(&ctx->ethmac_mutex);
}

static zx_status_t ethmac_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    ecm_ctx_t* eth = ctx;

    zxlogf(TRACE, "%s: %s called\n", module_name, __FUNCTION__);

    // No options are supported
    if (options) {
        zxlogf(ERROR, "%s: unexpected options (0x%"PRIx32") to ethmac_query\n", module_name, options);
        return ZX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    info->mtu = eth->mtu;
    memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));

    return ZX_OK;
}

static void ethmac_stop(void* cookie) {
    zxlogf(TRACE, "%s: %s called\n", module_name, __FUNCTION__);
    ecm_ctx_t* ctx = cookie;
    mtx_lock(&ctx->ethmac_mutex);
    ctx->ethmac_ifc = NULL;
    mtx_unlock(&ctx->ethmac_mutex);
}

static zx_status_t ethmac_start(void* ctx_cookie, ethmac_ifc_t* ifc, void* ethmac_cookie) {
    zxlogf(TRACE, "%s: %s called\n", module_name, __FUNCTION__);
    ecm_ctx_t* ctx = ctx_cookie;
    zx_status_t status = ZX_OK;

    mtx_lock(&ctx->ethmac_mutex);
    if (ctx->ethmac_ifc) {
        status = ZX_ERR_ALREADY_BOUND;
    } else {
        ctx->ethmac_ifc = ifc;
        ctx->ethmac_cookie = ethmac_cookie;
        ctx->ethmac_ifc->status(ethmac_cookie, ctx->online ? ETH_STATUS_ONLINE : 0);
    }
    mtx_unlock(&ctx->ethmac_mutex);

    return status;
}

static zx_status_t queue_request(ecm_ctx_t* ctx, uint8_t* data, size_t length, usb_request_t* req) {
    req->header.length = length;
    ssize_t bytes_copied = usb_request_copyto(req, data, length, 0);
    if (bytes_copied < 0) {
        zxlogf(ERROR, "%s: failed to copy data into send txn (error %zd)\n", module_name, bytes_copied);
        return ZX_ERR_IO;
    }
    usb_request_queue(&ctx->usb, req);
    return ZX_OK;
}

static zx_status_t send_locked(ecm_ctx_t* ctx, ethmac_netbuf_t* netbuf) {
    uint8_t* byte_data = netbuf->data;
    size_t length = netbuf->len;

    // As per the CDC-ECM spec, we need to send a zero-length packet to signify the end of
    // transmission when the endpoint max packet size is a factor of the total transmission size.
    bool send_terminal_packet = (length % ctx->tx_endpoint.max_packet_size == 0);

    // Make sure that we can get all of the tx buffers we need to use
    usb_request_t* tx_req = list_remove_head_type(&ctx->tx_txn_bufs, usb_request_t, node);
    if (tx_req == NULL) {
        return ZX_ERR_SHOULD_WAIT;
    }
    usb_request_t* terminal_req;
    if (send_terminal_packet) {
        terminal_req = list_remove_head_type(&ctx->tx_txn_bufs, usb_request_t, node);
        if (terminal_req == NULL) {
            list_add_tail(&ctx->tx_txn_bufs, &tx_req->node);
            return ZX_ERR_SHOULD_WAIT;
        }
    }

    zx_status_t status;
    if ((status = queue_request(ctx, byte_data, length, tx_req)) != ZX_OK) {
        list_add_tail(&ctx->tx_txn_bufs, &tx_req->node);
        if (send_terminal_packet) {
            list_add_tail(&ctx->tx_txn_bufs, &terminal_req->node);
        }
        return status;
    }

    if (send_terminal_packet && (status = queue_request(ctx, byte_data, 0, tx_req)) != ZX_OK) {
        // This leaves us in a very awkward situation, since failing to send the zero-length
        // packet means the ethernet packet will be improperly terminated.
        list_add_tail(&ctx->tx_txn_bufs, &terminal_req->node);
        return status;
    }

    return ZX_OK;
}

static void usb_write_complete(usb_request_t* request, void* cookie) {
    ecm_ctx_t* ctx = cookie;

    if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
        usb_request_release(request);
        return;
    }

    mtx_lock(&ctx->tx_mutex);

    // Return transmission buffer to pool
    list_add_tail(&ctx->tx_txn_bufs, &request->node);

    if (request->response.status == ZX_ERR_IO_REFUSED) {
        zxlogf(TRACE, "%s: resetting transmit endpoint\n", module_name);
        usb_reset_endpoint(&ctx->usb, ctx->tx_endpoint.addr);
    }

    bool additional_tx_queued = false;
    ethmac_netbuf_t* netbuf;
    zx_status_t send_status = ZX_OK;
    if (!list_is_empty(&ctx->tx_pending_infos)) {
        netbuf = list_peek_head_type(&ctx->tx_pending_infos, ethmac_netbuf_t, node);
        if ((send_status = send_locked(ctx, netbuf)) != ZX_ERR_SHOULD_WAIT) {
            list_remove_head(&ctx->tx_pending_infos);
            additional_tx_queued = true;
        }
    }

    mtx_unlock(&ctx->tx_mutex);

    mtx_lock(&ctx->ethmac_mutex);
    if (additional_tx_queued && ctx->ethmac_ifc) {
        ctx->ethmac_ifc->complete_tx(ctx->ethmac_cookie, netbuf, send_status);
    }
    mtx_unlock(&ctx->ethmac_mutex);

    // When the interface is offline, the transaction will complete with status set to
    // ZX_ERR_IO_NOT_PRESENT. There's not much we can do except ignore it.
}

// Note: the assumption made here is that no rx transmissions will be processed in parallel,
// so we do not maintain an rx mutex.
static void usb_recv(ecm_ctx_t* ctx, usb_request_t* request) {
    size_t len = request->response.actual;

    uint8_t* read_data;
    zx_status_t status = usb_request_mmap(request, (void*)&read_data);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_request_mmap failed with status %d\n",
                module_name, status);
        return;
    }

    mtx_lock(&ctx->ethmac_mutex);
    if (ctx->ethmac_ifc) {
        ctx->ethmac_ifc->recv(ctx->ethmac_cookie, read_data, len, 0);
    }
    mtx_unlock(&ctx->ethmac_mutex);
}

static void usb_read_complete(usb_request_t* request, void* cookie) {
    ecm_ctx_t* ctx = cookie;

    if (request->response.status != ZX_OK) {
        zxlogf(TRACE, "%s: usb_read_complete called with status %d\n",
                module_name, (int)request->response.status);
    }

    if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
        usb_request_release(request);
        return;
    }

    if (request->response.status == ZX_ERR_IO_REFUSED) {
        zxlogf(TRACE, "%s: resetting receive endpoint\n", module_name);
        usb_reset_endpoint(&ctx->usb, ctx->rx_endpoint.addr);
    } else if (request->response.status == ZX_OK) {
        usb_recv(ctx, request);
    }

    usb_request_queue(&ctx->usb, request);
}

static zx_status_t ethmac_queue_tx(void* cookie, uint32_t options, ethmac_netbuf_t* netbuf) {
    ecm_ctx_t* ctx = cookie;
    size_t length = netbuf->len;
    zx_status_t status;

    if (length > ctx->mtu || length == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    zxlogf(SPEW, "%s: sending %zu bytes to endpoint 0x%"PRIx8"\n",
            module_name, length, ctx->tx_endpoint.addr);

    mtx_lock(&ctx->tx_mutex);
    if (ctx->unbound) {
        status = ZX_ERR_IO_NOT_PRESENT;
    } else {
        status = send_locked(ctx, netbuf);
        if (status == ZX_ERR_SHOULD_WAIT) {
            // No buffers available, queue it up
            list_add_tail(&ctx->tx_pending_infos, &netbuf->node);
        }
    }

    mtx_unlock(&ctx->tx_mutex);
    return status;
}

static zx_status_t ethmac_set_param(void *cookie, uint32_t param, int32_t value, void* data) {
    return ZX_ERR_NOT_SUPPORTED;
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = ethmac_query,
    .stop = ethmac_stop,
    .start = ethmac_start,
    .queue_tx = ethmac_queue_tx,
    .set_param = ethmac_set_param,
};

static void ecm_interrupt_complete(usb_request_t* request, void* cookie) {
    ecm_ctx_t* ctx = cookie;
    completion_signal(&ctx->completion);
}

static void ecm_handle_interrupt(ecm_ctx_t* ctx, usb_request_t* request) {
    if (request->response.actual < sizeof(usb_cdc_notification_t)) {
        zxlogf(ERROR, "%s: ignored interrupt (size = %ld)\n", module_name, (long)request->response.actual);
        return;
    }

    usb_cdc_notification_t usb_req;
    usb_request_copyfrom(request, &usb_req, sizeof(usb_cdc_notification_t), 0);
    if (usb_req.bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
        usb_req.bNotification == USB_CDC_NC_NETWORK_CONNECTION) {
        ecm_update_online_status(ctx, usb_req.wValue != 0);
    } else if (usb_req.bmRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
               usb_req.bNotification == USB_CDC_NC_CONNECTION_SPEED_CHANGE) {
        // The ethermac driver doesn't care about speed changes, so even though we track this
        // information, it's currently unused.
        if (usb_req.wLength != 8) {
            zxlogf(ERROR, "%s: invalid size (%"PRIu16") for CONNECTION_SPEED_CHANGE notification\n",
                   module_name, usb_req.wLength);
            return;
        }
        // Data immediately follows notification in packet
        uint32_t new_us_bps, new_ds_bps;
        usb_request_copyfrom(request, &new_us_bps, 4, sizeof(usb_cdc_notification_t));
        usb_request_copyfrom(request, &new_ds_bps, 4, sizeof(usb_cdc_notification_t) + 4);
        if (new_us_bps != ctx->us_bps) {
            zxlogf(ERROR, "%s: connection speed change... upstream bits/s: %"PRIu32"\n",
                    module_name, new_us_bps);
            ctx->us_bps = new_us_bps;
        }
        if (new_ds_bps != ctx->ds_bps) {
            zxlogf(ERROR, "%s: connection speed change... downstream bits/s: %"PRIu32"\n",
                    module_name, new_ds_bps);
            ctx->ds_bps = new_ds_bps;
        }
    }  else {
        zxlogf(ERROR, "%s: ignored interrupt (type = %"PRIu8", request = %"PRIu8")\n",
               module_name, usb_req.bmRequestType, usb_req.bNotification);
        return;
    }
}

static int ecm_int_handler_thread(void* cookie) {
    ecm_ctx_t* ctx = cookie;
    usb_request_t* txn = ctx->int_txn_buf;

    while (true) {
        completion_reset(&ctx->completion);
        usb_request_queue(&ctx->usb, txn);
        completion_wait(&ctx->completion, ZX_TIME_INFINITE);
        if (txn->response.status == ZX_OK) {
            ecm_handle_interrupt(ctx, txn);
        } else if (txn->response.status == ZX_ERR_PEER_CLOSED ||
                   txn->response.status == ZX_ERR_IO_NOT_PRESENT) {
            zxlogf(TRACE, "%s: terminating interrupt handling thread\n", module_name);
            return txn->response.status;
        } else if (txn->response.status == ZX_ERR_IO_REFUSED) {
            zxlogf(TRACE, "%s: resetting interrupt endpoint\n", module_name);
            usb_reset_endpoint(&ctx->usb, ctx->int_endpoint.addr);
        } else {
            zxlogf(ERROR, "%s: error (%ld) waiting for interrupt - ignoring\n",
                   module_name, (long)txn->response.status);
        }
    }
}

static bool parse_cdc_header(usb_cs_header_interface_descriptor_t* header_desc) {
    // Check for supported CDC version
    zxlogf(TRACE, "%s: device reports CDC version as 0x%x\n", module_name, header_desc->bcdCDC);
    return header_desc->bcdCDC >= CDC_SUPPORTED_VERSION;
}

static bool parse_cdc_ethernet_descriptor(ecm_ctx_t* ctx,
                                          usb_cs_ethernet_interface_descriptor_t* desc) {
    ctx->mtu = desc->wMaxSegmentSize;

    // MAC address is stored in a string descriptor in UTF-16 format, so we get one byte of
    // address for each 32 bits of text.
    const size_t expected_str_size = sizeof(usb_string_descriptor_t) + ETH_MAC_SIZE * 4;
    char str_desc_buf[expected_str_size];

    // Read string descriptor for MAC address (string index is in iMACAddress field)
    size_t out_length;
    zx_status_t result = usb_get_descriptor(&ctx->usb, 0, USB_DT_STRING, desc->iMACAddress,
                                            str_desc_buf, sizeof(str_desc_buf), ZX_TIME_INFINITE,
                                            &out_length);
    if (result < 0) {
        zxlogf(ERROR, "%s: error reading MAC address\n", module_name);
        return false;
    }
    if (out_length != expected_str_size) {
        zxlogf(ERROR, "%s: MAC address string incorrect length (saw %zd, expected %zd)\n",
               module_name, out_length, expected_str_size);
        return false;
    }

    // Convert MAC address to something more machine-friendly
    usb_string_descriptor_t* str_desc = (usb_string_descriptor_t*)str_desc_buf;
    uint8_t* str = str_desc->bString;
    size_t ndx;
    for (ndx = 0; ndx < ETH_MAC_SIZE * 4; ndx++) {
        if (ndx % 2 == 1) {
            if (str[ndx] != 0) {
                zxlogf(ERROR, "%s: MAC address contains invalid characters\n", module_name);
                return false;
            }
            continue;
        }
        uint8_t value;
        if (str[ndx] >= '0' && str[ndx] <= '9') {
            value = str[ndx] - '0';
        } else if (str[ndx] >= 'A' && str[ndx] <= 'F') {
            value = (str[ndx] - 'A') + 0xa;
        } else {
            zxlogf(ERROR, "%s: MAC address contains invalid characters\n", module_name);
            return false;
        }
        if (ndx % 4 == 0) {
            ctx->mac_addr[ndx/4] = value << 4;
        } else {
            ctx->mac_addr[ndx/4] |= value;
        }
    }

    zxlogf(ERROR, "%s: MAC address is %02X:%02X:%02X:%02X:%02X:%02X\n", module_name,
           ctx->mac_addr[0], ctx->mac_addr[1], ctx->mac_addr[2],
            ctx->mac_addr[3], ctx->mac_addr[4], ctx->mac_addr[5]);
    return true;
}

static void copy_endpoint_info(ecm_endpoint_t* ep_info, usb_endpoint_descriptor_t* desc) {
    ep_info->addr = desc->bEndpointAddress;
    ep_info->max_packet_size = desc->wMaxPacketSize;
}

static bool want_interface(usb_interface_descriptor_t* intf, void* arg) {
    return intf->bInterfaceClass == USB_CLASS_CDC;
}

static zx_status_t ecm_bind(void* ctx, zx_device_t* device) {
    zxlogf(TRACE, "%s: starting %s\n", module_name, __FUNCTION__);

    usb_protocol_t usb;
    zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
    if (result != ZX_OK) {
        return result;
    }

    // Allocate context
    ecm_ctx_t* ecm_ctx = calloc(1, sizeof(ecm_ctx_t));
    if (!ecm_ctx) {
        zxlogf(ERROR, "%s: failed to allocate memory for USB CDC ECM driver\n", module_name);
        return ZX_ERR_NO_MEMORY;
    }

    result = usb_claim_additional_interfaces(&usb, want_interface, NULL);
    if (result != ZX_OK) {
        goto fail;
    }
    // Initialize context
    ecm_ctx->usb_device = device;
    memcpy(&ecm_ctx->usb, &usb, sizeof(ecm_ctx->usb));
    list_initialize(&ecm_ctx->tx_txn_bufs);
    list_initialize(&ecm_ctx->tx_pending_infos);
    mtx_init(&ecm_ctx->ethmac_mutex, mtx_plain);
    mtx_init(&ecm_ctx->tx_mutex, mtx_plain);
    ecm_ctx->ticks_per_second = zx_ticks_per_second();

    usb_desc_iter_t iter;
    result = usb_desc_iter_init(&usb, &iter);
    if (result != ZX_OK) {
        goto fail;
    }
    result = ZX_ERR_NOT_SUPPORTED;

    // Find the CDC descriptors and endpoints
    usb_descriptor_header_t* desc = usb_desc_iter_next(&iter);
    usb_cs_header_interface_descriptor_t* cdc_header_desc = NULL;
    usb_cs_ethernet_interface_descriptor_t* cdc_eth_desc = NULL;
    usb_endpoint_descriptor_t* int_ep = NULL;
    usb_endpoint_descriptor_t* tx_ep = NULL;
    usb_endpoint_descriptor_t* rx_ep = NULL;
    usb_interface_descriptor_t* default_ifc = NULL;
    usb_interface_descriptor_t* data_ifc = NULL;
    while (desc) {
        if (desc->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* ifc_desc = (void*)desc;
            if (ifc_desc->bInterfaceClass == USB_CLASS_CDC) {
                if (ifc_desc->bNumEndpoints == 0) {
                    if (default_ifc) {
                        zxlogf(ERROR, "%s: multiple default interfaces found\n", module_name);
                        goto fail;
                    }
                    default_ifc = ifc_desc;
                } else if (ifc_desc->bNumEndpoints == 2) {
                    if (data_ifc) {
                        zxlogf(ERROR, "%s: multiple data interfaces found\n", module_name);
                        goto fail;
                    }
                    data_ifc = ifc_desc;
                }
            }
        } else if (desc->bDescriptorType == USB_DT_CS_INTERFACE) {
            usb_cs_interface_descriptor_t* cs_ifc_desc = (void*)desc;
            if (cs_ifc_desc->bDescriptorSubType == USB_CDC_DST_HEADER) {
                if (cdc_header_desc != NULL) {
                    zxlogf(ERROR, "%s: multiple CDC headers\n", module_name);
                    goto fail;
                }
                cdc_header_desc = (void*)cs_ifc_desc;
            } else if (cs_ifc_desc->bDescriptorSubType == USB_CDC_DST_ETHERNET) {
                if (cdc_eth_desc != NULL) {
                    zxlogf(ERROR, "%s: multiple CDC ethernet descriptors\n", module_name);
                    goto fail;
                }
                cdc_eth_desc = (void*)cs_ifc_desc;
            }
        } else if (desc->bDescriptorType == USB_DT_ENDPOINT) {
            usb_endpoint_descriptor_t* endpoint_desc = (void*)desc;
            if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
                usb_ep_type(endpoint_desc) == USB_ENDPOINT_INTERRUPT) {
                if (int_ep != NULL) {
                    zxlogf(ERROR, "%s: multiple interrupt endpoint descriptors\n", module_name);
                    goto fail;
                }
                int_ep = endpoint_desc;
            } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_OUT &&
                       usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
                if (tx_ep != NULL) {
                    zxlogf(ERROR, "%s: multiple tx endpoint descriptors\n", module_name);
                    goto fail;
                }
                tx_ep = endpoint_desc;
            } else if (usb_ep_direction(endpoint_desc) == USB_ENDPOINT_IN &&
                       usb_ep_type(endpoint_desc) == USB_ENDPOINT_BULK) {
                if (rx_ep != NULL) {
                    zxlogf(ERROR, "%s: multiple rx endpoint descriptors\n", module_name);
                    goto fail;
                }
                rx_ep = endpoint_desc;
            } else {
                zxlogf(ERROR, "%s: unrecognized endpoint\n", module_name);
                goto fail;
            }
        }
        desc = usb_desc_iter_next(&iter);
    }
    if (cdc_header_desc == NULL || cdc_eth_desc == NULL) {
        zxlogf(ERROR, "%s: CDC %s descriptor(s) not found", module_name,
               cdc_header_desc ? "ethernet" : cdc_eth_desc ? "header" : "ethernet and header");
        goto fail;
    }
    if (int_ep == NULL || tx_ep == NULL || rx_ep == NULL) {
        zxlogf(ERROR, "%s: missing one or more required endpoints\n", module_name);
        goto fail;
    }
    if (default_ifc == NULL) {
        zxlogf(ERROR, "%s: unable to find CDC default interface\n", module_name);
        goto fail;
    }
    if (data_ifc == NULL) {
        zxlogf(ERROR, "%s: unable to find CDC data interface\n", module_name);
        goto fail;
    }

    // Parse the information in the CDC descriptors
    if (!parse_cdc_header(cdc_header_desc)) {
        goto fail;
    }
    if (!parse_cdc_ethernet_descriptor(ecm_ctx, cdc_eth_desc)) {
        goto fail;
    }

    // Parse endpoint information
    copy_endpoint_info(&ecm_ctx->int_endpoint, int_ep);
    copy_endpoint_info(&ecm_ctx->tx_endpoint, tx_ep);
    copy_endpoint_info(&ecm_ctx->rx_endpoint, rx_ep);

    // Reset by selecting default interface followed by data interface. We can't start
    // queueing transactions until this is complete.
    usb_set_interface(&usb, default_ifc->bInterfaceNumber, default_ifc->bAlternateSetting);
    usb_set_interface(&usb, data_ifc->bInterfaceNumber, data_ifc->bAlternateSetting);

    // Allocate interrupt transaction buffer
    usb_request_t* int_buf;
    zx_status_t alloc_result = usb_request_alloc(&int_buf, ecm_ctx->int_endpoint.max_packet_size,
                                                 ecm_ctx->int_endpoint.addr);
    if (alloc_result != ZX_OK) {
        result = alloc_result;
        goto fail;
    }

    int_buf->complete_cb = ecm_interrupt_complete;
    int_buf->cookie = ecm_ctx;
    ecm_ctx->int_txn_buf = int_buf;

    // Allocate tx transaction buffers
    uint16_t tx_buf_sz = ecm_ctx->mtu;
#if MAX_TX_BUF_SZ < UINT16_MAX
    if (tx_buf_sz > MAX_TX_BUF_SZ) {
        zxlogf(ERROR, "%s: insufficient space for even a single tx buffer\n", module_name);
        goto fail;
    }
#endif
    size_t tx_buf_remain = MAX_TX_BUF_SZ;
    while (tx_buf_remain >= tx_buf_sz) {
        usb_request_t* tx_buf;
        zx_status_t alloc_result = usb_request_alloc(&tx_buf, tx_buf_sz,
                                                     ecm_ctx->tx_endpoint.addr);
        if (alloc_result != ZX_OK) {
            result = alloc_result;
            goto fail;
        }

        tx_buf->complete_cb = usb_write_complete;
        tx_buf->cookie = ecm_ctx;
        list_add_head(&ecm_ctx->tx_txn_bufs, &tx_buf->node);
        tx_buf_remain -= tx_buf_sz;
    }

    // Allocate rx transaction buffers
    uint16_t rx_buf_sz = ecm_ctx->mtu;
#if MAX_TX_BUF_SZ < UINT16_MAX
    if (rx_buf_sz > MAX_RX_BUF_SZ) {
        zxlogf(ERROR, "%s: insufficient space for even a single rx buffer\n", module_name);
        goto fail;
    }
#endif
    size_t rx_buf_remain = MAX_RX_BUF_SZ;
    while (rx_buf_remain >= rx_buf_sz) {
        usb_request_t* rx_buf;
        zx_status_t alloc_result = usb_request_alloc(&rx_buf, rx_buf_sz,
                                                     ecm_ctx->rx_endpoint.addr);
        if (alloc_result != ZX_OK) {
            result = alloc_result;
            goto fail;
        }

        rx_buf->complete_cb = usb_read_complete;
        rx_buf->cookie = ecm_ctx;
        usb_request_queue(&ecm_ctx->usb, rx_buf);
        rx_buf_remain -= rx_buf_sz;
    }

    // Kick off the handler thread
    int thread_result = thrd_create_with_name(&ecm_ctx->int_thread, ecm_int_handler_thread,
                                              ecm_ctx, "ecm_int_handler_thread");
    if (thread_result != thrd_success) {
        zxlogf(ERROR, "%s: failed to create interrupt handler thread (%d)\n", module_name, thread_result);
        goto fail;
    }

    // Add the device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-cdc-ecm",
        .ctx = ecm_ctx,
        .ops = &ecm_device_proto,
        .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
        .proto_ops = &ethmac_ops,
    };
    result = device_add(ecm_ctx->usb_device, &args, &ecm_ctx->zxdev);
    if (result < 0) {
        zxlogf(ERROR, "%s: failed to add device: %d\n", module_name, (int)result);
        goto fail;
    }

    usb_desc_iter_release(&iter);
    return ZX_OK;

fail:
    usb_desc_iter_release(&iter);
    ecm_free(ecm_ctx);
    zxlogf(ERROR, "%s: failed to bind\n", module_name);
    return result;
}

static zx_driver_ops_t ecm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ecm_bind,
};

ZIRCON_DRIVER_BEGIN(ethernet_usb_cdc_ecm, ecm_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_COMM),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_CDC_SUBCLASS_ETHERNET),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0),
ZIRCON_DRIVER_END(ethernet_usb_cdc_ecm)
