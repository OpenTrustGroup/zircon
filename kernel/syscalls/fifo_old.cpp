// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// zx_fifo_read_old and zx_fifo_write_old: the fifo calls as upstream had them
// before they took an element size, for code written against that interface.
// The buffer's size is in bytes and is rounded down to a whole number of the
// fifo's elements; the number of elements moved is returned.

#include <object/fifo_dispatcher.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>

#include <fbl/ref_ptr.h>
#include <zircon/types.h>

#include "priv.h"

zx_status_t sys_fifo_write_old(zx_handle_t handle, user_in_ptr<const void> entries, size_t len,
                               user_out_ptr<uint32_t> actual_out) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<FifoDispatcher> fifo;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &fifo);
    if (status != ZX_OK)
        return status;

    size_t actual;
    status = fifo->WriteFromUser(fifo->elem_size(), entries.reinterpret<const uint8_t>(),
                                 len / fifo->elem_size(), &actual);
    if (status != ZX_OK)
        return status;

    return actual_out.copy_to_user(static_cast<uint32_t>(actual));
}

zx_status_t sys_fifo_read_old(zx_handle_t handle, user_out_ptr<void> entries, size_t len,
                              user_out_ptr<uint32_t> actual_out) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<FifoDispatcher> fifo;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &fifo);
    if (status != ZX_OK)
        return status;

    size_t actual;
    status = fifo->ReadToUser(fifo->elem_size(), entries.reinterpret<uint8_t>(),
                              len / fifo->elem_size(), &actual);
    if (status != ZX_OK)
        return status;

    return actual_out.copy_to_user(static_cast<uint32_t>(actual));
}
