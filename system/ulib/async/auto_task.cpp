// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/auto_task.h>

#include <zircon/assert.h>

namespace async {

AutoTask::AutoTask(async_t* async, zx_time_t deadline, uint32_t flags)
    : async_task_t{{ASYNC_STATE_INIT}, &AutoTask::CallHandler, deadline, flags, {}},
      async_(async) {
    ZX_DEBUG_ASSERT(async_);
}

AutoTask::~AutoTask() {
    Cancel();
}

zx_status_t AutoTask::Post() {
    ZX_DEBUG_ASSERT(!pending_);

    zx_status_t status = async_post_task(async_, this);
    if (status == ZX_OK)
        pending_ = true;

    return status;
}

void AutoTask::Cancel() {
    if (!pending_)
        return;

    zx_status_t status = async_cancel_task(async_, this);
    ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "status=%d", status);

    pending_ = false;
}

async_task_result_t AutoTask::CallHandler(async_t* async, async_task_t* task,
                                          zx_status_t status) {
    auto self = static_cast<AutoTask*>(task);
    ZX_DEBUG_ASSERT(self->pending_);
    self->pending_ = false;

    async_task_result_t result = self->handler_(async, status);
    if (result == ASYNC_TASK_REPEAT && status == ZX_OK) {
        ZX_DEBUG_ASSERT(!self->pending_);
        self->pending_ = true;
    }
    return result;
}

} // namespace async
