// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/counter.h>

namespace cobalt_client {
namespace internal {

BaseCounter::BaseCounter(BaseCounter&& other) : counter_(other.Exchange(0)) {}

RemoteCounter::RemoteCounter(uint64_t metric_id, const fbl::Vector<Metadata>& metadata)
    : BaseCounter(), buffer_(metadata), metric_id_(metric_id) {
    *buffer_.mutable_event_data() = 0;
}

RemoteCounter::RemoteCounter(RemoteCounter&& other)
    : BaseCounter(fbl::move(other)), buffer_(fbl::move(other.buffer_)),
      metric_id_(other.metric_id_) {}

bool RemoteCounter::Flush(const RemoteCounter::FlushFn& flush_handler) {
    if (!buffer_.TryBeginFlush()) {
        return false;
    }
    // Write the current value of the counter to the buffer, and reset it to 0.
    *buffer_.mutable_event_data() = static_cast<uint32_t>(this->Exchange());
    flush_handler(metric_id_, buffer_, fbl::BindMember(&buffer_, &EventBuffer::CompleteFlush));
    return true;
}
} // namespace internal

Counter::Counter(internal::RemoteCounter* remote_counter) : remote_counter_(remote_counter) {}

void Counter::Increment(Counter::Count value) {
    remote_counter_->Increment(value);
}

Counter::Count Counter::GetRemoteCount() const {
    return remote_counter_->Load();
}

} // namespace cobalt_client
