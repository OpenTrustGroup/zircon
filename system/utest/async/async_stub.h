// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>

struct AsyncStub : public async_t {
public:
    AsyncStub();
    virtual ~AsyncStub();

    virtual zx_status_t BeginWait(async_wait_t* wait);
    virtual zx_status_t CancelWait(async_wait_t* wait);
    virtual zx_status_t PostTask(async_task_t* task);
    virtual zx_status_t CancelTask(async_task_t* task);
    virtual zx_status_t QueuePacket(async_receiver_t* receiver,
                                    const zx_packet_user_t* data);
    virtual zx_status_t SetGuestBellTrap(async_guest_bell_trap_t* trap);
};
