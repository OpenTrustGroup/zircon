// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/types-internal.h>
#include <lib/fidl/cpp/vector_view.h>

namespace cobalt_client {
namespace internal {

template <typename T>
EventBuffer<T>::EventBuffer(const fbl::Vector<Metadata>& metadata) : flushing_(false) {
    metadata_.reserve(metadata.size() + 1);
    for (const auto& data : metadata) {
        metadata_.push_back(data);
    }
}

template <typename T> EventBuffer<T>::EventBuffer(EventBuffer&& other) {
    flushing_.store(other.flushing_.load());
    buffer_ = fbl::move(other.buffer_);
    metadata_ = fbl::move(other.metadata_);
}

template <typename T> EventBuffer<T>::~EventBuffer() = default;

// Supported types for cobalt's metric types.
// Counter.
template class EventBuffer<uint32_t>;
// Histogram.
template class EventBuffer<fidl::VectorView<HistogramBucket>>;

} // namespace internal
} // namespace cobalt_client
