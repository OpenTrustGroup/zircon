// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/receiver.h>

namespace async {

Receiver::Receiver(uint32_t flags)
    : async_receiver_t{{ASYNC_STATE_INIT}, &Receiver::CallHandler, flags, {}} {}

Receiver::~Receiver() = default;

zx_status_t Receiver::Queue(async_t* async, const zx_packet_user_t* data) {
    return async_queue_packet(async, this, data);
}

void Receiver::CallHandler(async_t* async, async_receiver_t* receiver,
                           zx_status_t status, const zx_packet_user_t* data) {
    static_cast<Receiver*>(receiver)->handler_(async, status, data);
}

} // namespace async
