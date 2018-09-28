// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace cobalt_client {
namespace internal {
// Forward declaration.
class RemoteCounter;
} // namespace internal

// Thin wrapper for an atomic counter with a fixed memory order. The counter handles
// a remote count and a local count. The remote count is periodically flushed, while
// the local count is viewed on demand (and optionally flushed depending on configuration).
//
// This class is copyable, moveable and assignable.
// This class is thread-safe.
class Counter {
public:
    // Underlying type used for representing an actual counter.
    using Count = uint64_t;

    Counter() = delete;
    Counter(internal::RemoteCounter* remote_counter);
    Counter(const Counter& other) : remote_counter_(other.remote_counter_){};
    Counter(Counter&&) = default;
    ~Counter() = default;

    // Increments the counter value by |value|. This applies to local and remote
    // values of the counter.
    void Increment(Count value = 1);

    // Returns the current value of the counter that would be
    // sent to the remote service(cobalt).
    Count GetRemoteCount() const;

private:
    // Implementation of the flushable counter. The value
    // of this counter is flushed by the collector.
    internal::RemoteCounter* remote_counter_;
};

} // namespace cobalt_client
