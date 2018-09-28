// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <fbl/atomic.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/vmo.h>

namespace cobalt_client {
namespace internal {
// Forward Declarations.
class RemoteHistogram;
class RemoteCounter;
struct Metadata;
class Logger;
} // namespace internal

// Defines the options for initializing the Collector.
struct CollectorOptions {
    // Callback used when reading the config to create a cobalt logger.
    // Returns true when the write was successfull. The VMO will be transferred
    // to the cobalt service.
    fbl::Function<bool(zx::vmo*)> load_config;

    // We need this information for pre-allocating storage
    // and guaranteeing no dangling pointers, plus contiguos
    // memory for cache friendlyness.

    // Number of histograms to be used.
    size_t max_histograms;

    // Number of counters to be used.
    size_t max_counters;
};

// This class acts as a peer for instantiating Hisotgrams and Counters. All
// objects instantiated through this class act as a view, which means that
// their lifetime is coupled to this object's lifetime. This class does require
// the number of different configurations on construction.
//
// The Sink provides an API for persisiting the supported data types. This is
// exposed to simplify testing.
//
// This class is moveable, but not copyable or assignable.
// This class is thread-compatible.
class Collector {
public:
    // TODO(gevalentino): Once the cobalt client is written add a factory method to return
    // an instance of |Collector|. The cobalt client will implement the logger interface,
    // we do this to simplify testing.
    // static Collector Create(const Collector& options);

    Collector(const CollectorOptions& options, fbl::unique_ptr<internal::Logger> logger);
    Collector(const Collector&) = delete;
    Collector(Collector&&);
    Collector& operator=(const Collector&) = delete;
    Collector& operator=(Collector&&) = delete;
    ~Collector();

    // Returns a histogram to log events for a given |metric_id|, |event_type_index|
    // on a histogram described by |options|.
    Histogram AddHistogram(uint64_t metric_id, uint32_t event_type_index,
                           const HistogramOptions& options);

    // Returns a counter to log events for a given |metric_id| and |event_type_index|
    // as a raw counter.
    Counter AddCounter(uint64_t metric_id, uint32_t event_type_index);

    // Flushes the content of all flushable metrics into |sink_|. The |sink_| is
    // in charge of persisting the data.
    void Flush();

private:
    void LogHistogram(internal::RemoteHistogram* histogram);
    void LogCounter(internal::RemoteCounter* counter);

    fbl::Vector<HistogramOptions> histogram_options_;
    fbl::Vector<internal::RemoteHistogram> remote_histograms_;
    fbl::Vector<internal::RemoteCounter> remote_counters_;

    fbl::unique_ptr<internal::Logger> logger_;
    fbl::atomic<bool> flushing_;
};

} // namespace cobalt_client
