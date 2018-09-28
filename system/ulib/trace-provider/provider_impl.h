// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <trace-engine/types.h>
#include <trace-provider/provider.h>

// Provide a definition for the opaque type declared in provider.h.
struct trace_provider {};

namespace trace {
namespace internal {

class TraceProviderImpl final : public trace_provider_t {
public:
    TraceProviderImpl(async_dispatcher_t* dispatcher, zx::channel channel);
    ~TraceProviderImpl();

private:
    class Connection final {
    public:
        Connection(TraceProviderImpl* impl, zx::channel channel);
        ~Connection();

    private:
        void Handle(async_dispatcher_t* dispatcher,
                    async::WaitBase* wait,
                    zx_status_t status,
                    const zx_packet_signal_t* signal);

        bool ReadMessage();
        bool DecodeAndDispatch(uint8_t* buffer, uint32_t num_bytes,
                               zx_handle_t* handles, uint32_t num_handles);
        void Close();

        TraceProviderImpl* const impl_;
        zx::channel channel_;
        async::WaitMethod<Connection, &Connection::Handle> wait_;
    };

    void Start(trace_buffering_mode_t buffering_mode, zx::vmo buffer,
               zx::fifo fifo, fbl::Vector<fbl::String> enabled_categories);
    void Stop();
    void OnClose();

    async_dispatcher_t* const dispatcher_;
    Connection connection_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(TraceProviderImpl);
};

} // namespace internal
} // namespace trace
