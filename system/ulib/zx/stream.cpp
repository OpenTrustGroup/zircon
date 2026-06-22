// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/stream.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t stream::create(uint32_t options, const vmo& vmo_handle, zx_off_t seek,
                           stream* out_stream) {
    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t status = zx_stream_create(options, vmo_handle.get(), seek, &h);
    out_stream->reset(h);
    return status;
}

} // namespace zx
