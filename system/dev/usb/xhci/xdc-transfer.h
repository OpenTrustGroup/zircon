// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "xdc.h"

// Restarts a stopped transfer ring. All TRBs queued on the transfer ring are
// converted to NO-OPs, and will attempt to reschedule previously pending requests.
zx_status_t xdc_restart_transfer_ring_locked(xdc_t* xdc, xdc_endpoint_t* ep)
                                             __TA_REQUIRES(xdc->lock);

zx_status_t xdc_queue_transfer(xdc_t* xdc, usb_request_t* req, bool in);
bool xdc_has_free_trbs(xdc_t* xdc, bool in);

void xdc_handle_transfer_event_locked(xdc_t* xdc, xhci_trb_t* trb) __TA_REQUIRES(xdc->lock);