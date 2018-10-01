// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/resource.h>
#include <zircon/types.h>

namespace gzos_utils {

zx_status_t get_shm_resource(zx::resource* resource);

}
