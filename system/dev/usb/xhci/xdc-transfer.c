// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "xdc.h"
#include "xdc-transfer.h"

// Returns ZX_OK if the request was scheduled successfully, or ZX_ERR_SHOULD_WAIT
// if we ran out of TRBs.
static zx_status_t xdc_schedule_transfer_locked(xdc_t* xdc, xdc_endpoint_t* ep,
                                                usb_request_t* req) __TA_REQUIRES(ep->lock) {
    xhci_transfer_ring_t* ring = &ep->transfer_ring;

    // Need to clean the cache for both IN and OUT transfers, invalidate only for IN.
    if (ep->direction == USB_DIR_IN) {
        usb_request_cache_flush_invalidate(req, 0, req->header.length);
    } else {
        usb_request_cache_flush(req, 0, req->header.length);
    }

    zx_status_t status = xhci_queue_data_trbs(ring, &ep->transfer_state, req,
                                              0 /* interrupter */, false /* isochronous */);
    if (status != ZX_OK) {
        return status;
    }

    // If we get here, then we are ready to ring the doorbell.
    // Save the ring position so we can update the ring dequeue ptr once the transfer completes.
    req->context = (void *)ring->current;

    uint8_t doorbell_val = ep->direction == USB_DIR_IN ? DCDB_DB_EP_IN : DCDB_DB_EP_OUT;
    XHCI_SET_BITS32(&xdc->debug_cap_regs->dcdb, DCDB_DB_START, DCDB_DB_BITS, doorbell_val);

    return ZX_OK;
}

// Schedules any queued requests on the endpoint's transfer ring, until we fill our
// transfer ring or have no more requests.
static void xdc_process_transactions_locked(xdc_t* xdc, xdc_endpoint_t* ep)
                                            __TA_REQUIRES(ep->lock) {
    while (1) {
        if (xhci_transfer_ring_free_trbs(&ep->transfer_ring) == 0) {
            // No available TRBs - need to wait for some to complete.
            return;
        }

        if (!ep->current_req) {
            // Start the next transaction in the queue.
            usb_request_t* req = list_remove_head_type(&ep->queued_reqs, usb_request_t, node);
            if (!req) {
                // No requests waiting.
                return;
            }
            xhci_transfer_state_init(&ep->transfer_state, req,
                                     USB_ENDPOINT_BULK, EP_CTX_MAX_PACKET_SIZE);
            list_add_tail(&ep->pending_reqs, &req->node);
            ep->current_req = req;
        }

        usb_request_t* req = ep->current_req;
        zx_status_t status = xdc_schedule_transfer_locked(xdc, ep, req);
        if (status == ZX_ERR_SHOULD_WAIT) {
            // No available TRBs - need to wait for some to complete.
            return;
        } else {
            ep->current_req = NULL;
        }
    }
}

zx_status_t xdc_queue_transfer(xdc_t* xdc, usb_request_t* req, bool in) {
    xdc_endpoint_t* ep = in ? &xdc->eps[IN_EP_IDX] : &xdc->eps[OUT_EP_IDX];

    mtx_lock(&ep->lock);

    mtx_lock(&xdc->configured_mutex);

    // Make sure we're recently checked the device state registers.
    xdc_update_configuration_state_locked(xdc);
    xdc_update_endpoint_state_locked(xdc, ep);

    if (!xdc->configured || ep->state == XDC_EP_STATE_DEAD) {
        mtx_unlock(&xdc->configured_mutex);
        mtx_unlock(&ep->lock);
        return ZX_ERR_IO_NOT_PRESENT;
    }
    mtx_unlock(&xdc->configured_mutex);

    if (req->header.length > 0) {
        zx_status_t status = usb_request_physmap(req);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: usb_request_physmap failed: %d\n", __FUNCTION__, status);
            // Call the complete callback outside of the lock.
            mtx_unlock(&ep->lock);
            usb_request_complete(req, status, 0);
            return ZX_OK;
        }
    }

    list_add_tail(&ep->queued_reqs, &req->node);

    // We can still queue requests for later while the endpoint is halted,
    // but before scheduling the TRBs we should wait until the halt is
    // cleared by DbC and we've cleaned up the transfer ring.
    if (ep->state == XDC_EP_STATE_RUNNING) {
        xdc_process_transactions_locked(xdc, ep);
    }

    mtx_unlock(&ep->lock);

    return ZX_OK;
}