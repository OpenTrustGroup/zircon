// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-bus.h>

zx_status_t aml_mali_init(platform_bus_protocol_t* pbus, uint32_t bti_index);
