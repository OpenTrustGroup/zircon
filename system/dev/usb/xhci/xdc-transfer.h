// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "xdc.h"

zx_status_t xdc_queue_transfer(xdc_t* xdc, usb_request_t* req, bool in);