// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-engine/handler.h>

#include <stdio.h>
#include <string.h>

#include <zircon/assert.h>

#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/event.h>
#include <trace-engine/instrumentation.h>

#include "context_impl.h"

namespace {

// Amount of time to allow for other threads to release their references
// to the trace buffer during shutdown.  See point of use for details.
constexpr zx::duration kSynchronousShutdownTimeout = zx::msec(1000);

// Trace engine lock.
// See rules below for how this is used.
fbl::Mutex g_engine_mutex;

// Trace instrumentation state.
// Rules:
//   - can only be modified while holding g_engine_mutex
//   - can be read atomically at any time
fbl::atomic<int> g_state{TRACE_STOPPED};

// Trace disposition.
// This is the status that will be reported to the trace handler when the
// trace finishes.
// Rules:
//   - can only be accessed or modified while holding g_engine_mutex
zx_status_t g_disposition __TA_GUARDED(g_engine_mutex) {ZX_OK};

// Trace asynchronous dispatcher.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock only while the engine is not stopped
async_dispatcher_t* g_dispatcher{nullptr};

// Trace handler.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock only while the engine is not stopped
trace_handler_t* g_handler{nullptr};

// Trace observer table.
// Rules:
//   - can only be accessed or modified while holding g_engine_mutex
struct Observer {
    // The event handle that we notify the observer through.
    zx_handle_t event;
    // Set to true when the engine starts to indicate we're waiting for this
    // observer to call us back, via |trace_notify_observer_updated()|, that
    // it has started. When it does call us back this is set back to false.
    bool awaiting_update_after_start;
};
fbl::Vector<Observer> g_observers __TA_GUARDED(g_engine_mutex);

// Trace context reference count.
// This functions as a non-exclusive lock for the engine's trace context.
// Rules:
//   - acquiring a reference acts as an ACQUIRE fence
//   - releasing a reference acts as a RELEASE fence
//   - always 0 when engine stopped
//   - transition from 0 to non-zero only happens when engine is started
//   - the engine stops when the reference count goes to 0
//     (in other words, holding a context reference prevents the engine from stopping)
//
// There are two separate counters here that collectively provide the full
// count: buffer acquisitions and prolonged acquisitions. Buffer acquisitions
// are for the purpose of writing to the trace buffer. Prolonged acquisitions
// are for things like adhoc trace providers where they want to maintain a
// reference to the context for the duration of the trace.
// Buffer acquisitions increment/decrement the count by
// |kBufferCounterIncrement|. Prolonged acquisitions increment/decrement the
// count by |kProlongedCounterIncrement|.
// To maintain the property that the full count only transitions from 0 to 1
// when the engine is started |kProlongedCounterIncrement| == 1.
fbl::atomic_uint32_t g_context_refs{0u};

// The uint32_t context ref count is split this way:
// |31 ... 8| = buffer acquisition count
// |7 ... 0| = prolonged acquisition count
// There are generally only a handful of prolonged acquisitions. The code will
// assert-fail if there are more. This allows for 2^24 buffer acquisitions
// which is basically 2^24 threads. The values are also chosen so that the
// full count is easily interpreted when printed in hex.
constexpr uint32_t kProlongedCounterShift = 0;
constexpr uint32_t kProlongedCounterIncrement = 1 << kProlongedCounterShift;
constexpr uint32_t kMaxProlongedCounter = 127;
constexpr uint32_t kProlongedCounterMask = 0xff;
constexpr uint32_t kBufferCounterShift = 8;
constexpr uint32_t kBufferCounterIncrement = 1 << kBufferCounterShift;
constexpr uint32_t kBufferCounterMask = 0xffffff00;

// Trace context.
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be accessed outside the lock while holding a context reference
trace_context_t* g_context{nullptr};

// Event for tracking:
// - when all observers has started
//   (SIGNAL_ALL_OBSERVERS_STARTED)
// - when the trace context reference count has dropped to zero
//   (SIGNAL_CONTEXT_RELEASED)
// Rules:
//   - can only be modified while holding g_engine_mutex and engine is stopped
//   - can be read outside the lock while the engine is not stopped
zx::event g_event;
constexpr zx_signals_t SIGNAL_ALL_OBSERVERS_STARTED = ZX_USER_SIGNAL_0;
constexpr zx_signals_t SIGNAL_CONTEXT_RELEASED = ZX_USER_SIGNAL_1;

// Asynchronous operations posted to the asynchronous dispatcher while the
// engine is running.  Use of these structures is guarded by the engine lock.
async_wait_t g_event_wait;

inline uint32_t get_prolonged_context_refs(uint32_t raw) {
    return (raw & kProlongedCounterMask) >> kProlongedCounterShift;
}

inline uint32_t get_buffer_context_refs(uint32_t raw) {
    return (raw & kBufferCounterMask) >> kBufferCounterShift;
}

void handle_event(async_dispatcher_t* dispatcher, async_wait_t* wait,
                  zx_status_t status, const zx_packet_signal_t* signal);

// must hold g_engine_mutex
inline void update_disposition_locked(zx_status_t disposition) __TA_REQUIRES(g_engine_mutex) {
    if (g_disposition == ZX_OK)
        g_disposition = disposition;
}

void notify_observers_locked() __TA_REQUIRES(g_engine_mutex) {
    for (auto& observer : g_observers) {
        zx_status_t status = zx_object_signal(observer.event, 0u, ZX_EVENT_SIGNALED);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }
}

void notify_engine_all_observers_started_if_needed_locked() __TA_REQUIRES(g_engine_mutex) {
    for (auto& item : g_observers) {
        if (item.awaiting_update_after_start)
            return;
    }
    g_event.signal(0u, SIGNAL_ALL_OBSERVERS_STARTED);
}

} // namespace

/*** Trace engine functions ***/

// thread-safe
zx_status_t trace_start_engine(async_dispatcher_t* dispatcher,
                               trace_handler_t* handler,
                               trace_buffering_mode_t buffering_mode,
                               void* buffer,
                               size_t buffer_num_bytes) {
    ZX_DEBUG_ASSERT(dispatcher);
    ZX_DEBUG_ASSERT(handler);
    ZX_DEBUG_ASSERT(buffer);

    switch (buffering_mode) {
    case TRACE_BUFFERING_MODE_ONESHOT:
    case TRACE_BUFFERING_MODE_CIRCULAR:
    case TRACE_BUFFERING_MODE_STREAMING:
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    // The buffer size must be a multiple of 4096 (simplifies buffer size
    // calcs).
    if ((buffer_num_bytes & 0xfff) != 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (buffer_num_bytes < trace_context::min_buffer_size() ||
        buffer_num_bytes > trace_context::max_buffer_size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock lock(&g_engine_mutex);

    // We must have fully stopped a prior tracing session before starting a new one.
    if (g_state.load(fbl::memory_order_relaxed) != TRACE_STOPPED)
        return ZX_ERR_BAD_STATE;
    ZX_DEBUG_ASSERT(g_context_refs.load(fbl::memory_order_relaxed) == 0u);

    zx::event event;
    zx_status_t status = zx::event::create(0u, &event);
    if (status != ZX_OK)
        return status;

    // Schedule a waiter for |event|.
    g_event_wait = {
        .state = {ASYNC_STATE_INIT},
        .handler = &handle_event,
        .object = event.get(),
        .trigger = (SIGNAL_ALL_OBSERVERS_STARTED |
                    SIGNAL_CONTEXT_RELEASED)};
    status = async_begin_wait(dispatcher, &g_event_wait);
    if (status != ZX_OK)
        return status;

    // Initialize the trace engine state and context.
    g_state.store(TRACE_STARTED, fbl::memory_order_relaxed);
    g_dispatcher = dispatcher;
    g_handler = handler;
    g_disposition = ZX_OK;
    g_context = new trace_context(buffer, buffer_num_bytes, buffering_mode, handler);
    g_event = fbl::move(event);

    g_context->InitBufferHeader();

    // Write the trace initialization record first before allowing clients to
    // get in and write their own trace records.
    trace_context_write_initialization_record(g_context, zx_ticks_per_second());

    // After this point clients can acquire references to the trace context.
    g_context_refs.store(kProlongedCounterIncrement, fbl::memory_order_release);

    // Notify observers that the state changed.
    if (g_observers.is_empty()) {
        g_event.signal(0u, SIGNAL_ALL_OBSERVERS_STARTED);
    } else {
        for (auto& observer : g_observers)
            observer.awaiting_update_after_start = true;
        notify_observers_locked();
    }

    return ZX_OK;
}

// thread-safe
zx_status_t trace_stop_engine(zx_status_t disposition) {
    fbl::AutoLock lock(&g_engine_mutex);

    // We must have have an active trace in order to stop it.
    int state = g_state.load(fbl::memory_order_relaxed);
    if (state == TRACE_STOPPED)
        return ZX_ERR_BAD_STATE;

    update_disposition_locked(disposition);
    if (state == TRACE_STOPPING)
        return ZX_OK; // already stopping

    ZX_DEBUG_ASSERT(state == TRACE_STARTED);
    ZX_DEBUG_ASSERT(g_context_refs.load(fbl::memory_order_relaxed) != 0u);

    // Begin stopping the trace.
    g_state.store(TRACE_STOPPING, fbl::memory_order_relaxed);

    // Notify observers that the state changed.
    notify_observers_locked();

    // Release the trace engine's own reference to the trace context.
    // |handle_context_released()| will be called asynchronously when the last
    // reference is released.
    trace_release_prolonged_context(reinterpret_cast<trace_prolonged_context_t*>(g_context));

    return ZX_OK;
}

// This is an internal function, only called from context.cpp.
// thread-safe
bool trace_engine_is_buffer_context_released() {
    return (g_context_refs.load(fbl::memory_order_relaxed) &
            kBufferCounterMask) == 0;
}

// This is an internal function, only called from context.cpp.
// thread-safe
void trace_engine_request_save_buffer(uint32_t wrapped_count,
                                      uint64_t durable_data_end) {
    // Handle the request on the engine's async loop. This may be get called
    // while servicing a client trace request, and we don't want to handle it
    // there.
    async::PostTask(g_dispatcher, [wrapped_count, durable_data_end] () {
        auto context = trace_acquire_prolonged_context();
        if (context) {
            auto tcontext = reinterpret_cast<trace_context_t*>(context);
            tcontext->HandleSaveRollingBufferRequest(wrapped_count, durable_data_end);
            trace_release_prolonged_context(context);
        }
    });
}

// This is called by the handler after it has saved a buffer.
// |wrapped_count| and |durable_end| are the values that were passed to it,
// and are passed back to us for sanity checking purposes.
// thread-safe
zx_status_t trace_engine_mark_buffer_saved(uint32_t wrapped_count,
                                           uint64_t durable_data_end) {
    auto context = trace_acquire_prolonged_context();

    // No point in updating if there's no active trace.
    if (!context) {
        return ZX_ERR_BAD_STATE;
    }

    // Do this now, instead of as a separate iteration on the async loop.
    // The concern is that we want to update buffer state ASAP to reduce the
    // window where records might be dropped because the buffer is full.
    auto tcontext = reinterpret_cast<trace_context_t*>(context);
    tcontext->MarkRollingBufferSaved(wrapped_count, durable_data_end);

    trace_release_prolonged_context(context);
    return ZX_OK;
}

namespace {

void handle_all_observers_started() {
    // TODO(TO-530): Allow indicating an observer failed to start.

    // Clear the signal, otherwise we'll keep getting called.
    g_event.signal(SIGNAL_ALL_OBSERVERS_STARTED, 0u);

    // Note: There's no race in the use of |g_handler| here. If it will be set
    // to NULL that will be done later (handle_context_released is called by
    // handle_event after we are).
    if (g_handler) {
        g_handler->ops->trace_started(g_handler);
    }
}

void handle_context_released(async_dispatcher_t* dispatcher) {
    // All ready to clean up.
    // Grab the mutex while modifying shared state.
    zx_status_t disposition;
    trace_handler_t* handler;
    size_t buffer_bytes_written;
    {
        fbl::AutoLock lock(&g_engine_mutex);

        ZX_DEBUG_ASSERT(g_state.load(fbl::memory_order_relaxed) == TRACE_STOPPING);
        ZX_DEBUG_ASSERT(g_context_refs.load(fbl::memory_order_relaxed) == 0u);
        ZX_DEBUG_ASSERT(g_context != nullptr);

        // Update final buffer state.
        g_context->UpdateBufferHeaderAfterStopped();

        // Get final disposition.
        if (g_context->WasRecordDropped())
            update_disposition_locked(ZX_ERR_NO_MEMORY);
        disposition = g_disposition;
        handler = g_handler;
        buffer_bytes_written = (g_context->RollingBytesAllocated() +
                                g_context->DurableBytesAllocated());

        // Tidy up.
        g_dispatcher = nullptr;
        g_handler = nullptr;
        g_disposition = ZX_OK;
        g_event.reset();
        delete g_context;
        g_context = nullptr;

        // After this point, it's possible for the engine to be restarted.
        g_state.store(TRACE_STOPPED, fbl::memory_order_relaxed);

        // Notify observers that the state changed.
        notify_observers_locked();
    }

    // Notify the handler about the final disposition.
    handler->ops->trace_stopped(handler, dispatcher, disposition, buffer_bytes_written);
}

// Handles the case where the asynchronous dispatcher has encountered an error
// and will no longer be servicing the wait callback.  Consequently, this is
// our last chance to stop the engine and await for all contexts to be released.
void handle_hard_shutdown(async_dispatcher_t* dispatcher) {
    // Stop the engine, in case it hasn't noticed yet.
    trace_stop_engine(ZX_ERR_CANCELED);

    // There may still be outstanding references to the trace context.
    // We don't know when or whether they will be cleared but we can't complete
    // shut down until they are gone since there might still be live pointers
    // into the trace buffer so allow a brief timeout.  If the release event
    // hasn't been signaled by then, declare the trace engine dead in the water
    // to prevent dangling pointers.  This situations should be very rare as it
    // only occurs when the asynchronous dispatcher is shutting down, typically
    // just prior to process exit.
    auto status = g_event.wait_one(
        SIGNAL_CONTEXT_RELEASED,
        zx::deadline_after(kSynchronousShutdownTimeout),
        nullptr);
    if (status == ZX_OK) {
        handle_context_released(dispatcher);
        return;
    }

    // Uh oh.
    auto context_refs = g_context_refs.load(fbl::memory_order_relaxed);
    fprintf(stderr,
            "TraceEngine: Timed out waiting for %u buffer, %u prolonged trace context\n"
            "references (raw 0x%x) to be released after %lu ns\n"
            "while the asynchronous dispatcher was shutting down.\n"
            "Tracing will no longer be available in this process.",
            get_buffer_context_refs(context_refs),
            get_prolonged_context_refs(context_refs),
            context_refs,
            kSynchronousShutdownTimeout.get());
}

void handle_event(async_dispatcher_t* dispatcher, async_wait_t* wait,
                  zx_status_t status, const zx_packet_signal_t* signal) {
    // Note: This function may get all signals at the same time.

    if (status == ZX_OK) {
        if (signal->observed & SIGNAL_ALL_OBSERVERS_STARTED) {
            handle_all_observers_started();
        }
        if (signal->observed & SIGNAL_CONTEXT_RELEASED) {
            handle_context_released(dispatcher);
            return; // trace engine is completely stopped now
        }
        status = async_begin_wait(dispatcher, &g_event_wait);
    }

    if (status != ZX_OK) {
        handle_hard_shutdown(dispatcher);
    }
}

} // namespace

/*** Trace instrumentation functions ***/

// thread-safe, lock-free
trace_state_t trace_state() {
    return static_cast<trace_state_t>(g_state.load(fbl::memory_order_relaxed));
}

// thread-safe
bool trace_is_category_enabled(const char* category_literal) {
    trace_context_t* context = trace_acquire_context();
    if (likely(!context))
        return false;
    bool result = trace_context_is_category_enabled(context, category_literal);
    trace_release_context(context);
    return result;
}

// thread-safe, fail-fast, lock-free
trace_context_t* trace_acquire_context() {
    // Fail fast: Check whether we could possibly write into the trace buffer.
    // The count must be at least 1 to indicate that the buffer is initialized.
    // This is marked likely because tracing is usually disabled and we want
    // to return as quickly as possible from this function.
    uint32_t count = g_context_refs.load(fbl::memory_order_relaxed);
    if (likely(count == 0u))
        return nullptr;

    // Attempt to increment the reference count.
    // This also acts as a fence for future access to buffer state variables.
    //
    // Note the ACQUIRE fence here since the trace context may have changed
    // from the perspective of this thread.
    while (!g_context_refs.compare_exchange_weak(&count,
                                                 count + kBufferCounterIncrement,
                                                 fbl::memory_order_acquire,
                                                 fbl::memory_order_relaxed)) {
        if (unlikely(count == 0u))
            return nullptr;
    }
    return g_context;
}

trace_context_t* trace_acquire_context_for_category(const char* category_literal,
                                                    trace_string_ref_t* out_ref) {
    // This is marked likely because tracing is usually disabled and we want
    // to return as quickly as possible from this function.
    trace_context_t* context = trace_acquire_context();
    if (likely(!context))
        return nullptr;

    if (!trace_context_register_category_literal(context, category_literal, out_ref)) {
        trace_release_context(context);
        return nullptr;
    }

    return context;
}

// thread-safe, never-fail, lock-free
void trace_release_context(trace_context_t* context) {
    ZX_DEBUG_ASSERT(context == g_context);
    ZX_DEBUG_ASSERT(get_buffer_context_refs(g_context_refs.load(fbl::memory_order_relaxed)) != 0u);

    // Note the RELEASE fence here since the trace context and trace buffer
    // contents may have changes from the perspective of other threads.
    auto previous = g_context_refs.fetch_sub(kBufferCounterIncrement,
                                             fbl::memory_order_release);
    if (unlikely(previous == kBufferCounterIncrement)) {
        // Notify the engine that the last reference was released.
        zx_status_t status = g_event.signal(0u, SIGNAL_CONTEXT_RELEASED);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }
}

// thread-safe, fail-fast, lock-free
trace_prolonged_context_t* trace_acquire_prolonged_context() {
    // There's no need for extreme efficiency here, but for consistency with
    // |trace_acquire_context()| we copy what it does.
    uint32_t count = g_context_refs.load(fbl::memory_order_relaxed);
    if (likely(count == 0u))
        return nullptr;

    // Attempt to increment the reference count.
    // This also acts as a fence for future access to buffer state variables.
    //
    // Note the ACQUIRE fence here since the trace context may have changed
    // from the perspective of this thread.
    while (!g_context_refs.compare_exchange_weak(&count,
                                                 count + kProlongedCounterIncrement,
                                                 fbl::memory_order_acquire,
                                                 fbl::memory_order_relaxed)) {
        if (likely(count == 0u))
            return nullptr;
    }
    ZX_DEBUG_ASSERT(get_prolonged_context_refs(g_context_refs.load(fbl::memory_order_relaxed)) <=
                    kMaxProlongedCounter);
    return reinterpret_cast<trace_prolonged_context_t*>(g_context);
}

// thread-safe, never-fail, lock-free
void trace_release_prolonged_context(trace_prolonged_context_t* context) {
    auto tcontext = reinterpret_cast<trace_context_t*>(context);
    ZX_DEBUG_ASSERT(tcontext == g_context);
    ZX_DEBUG_ASSERT(get_prolonged_context_refs(g_context_refs.load(fbl::memory_order_relaxed)) != 0u);

    // Note the RELEASE fence here since the trace context and trace buffer
    // contents may have changes from the perspective of other threads.
    auto previous = g_context_refs.fetch_sub(kProlongedCounterIncrement,
                                             fbl::memory_order_release);
    if (unlikely(previous == kProlongedCounterIncrement)) {
        // Notify the engine that the last reference was released.
        zx_status_t status = g_event.signal(0u, SIGNAL_CONTEXT_RELEASED);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }
}

zx_status_t trace_register_observer(zx_handle_t event) {
    fbl::AutoLock lock(&g_engine_mutex);

    for (const auto& item : g_observers) {
        if (item.event == event)
            return ZX_ERR_INVALID_ARGS;
    }

    g_observers.push_back(Observer{event, false});
    return ZX_OK;
}

zx_status_t trace_unregister_observer(zx_handle_t event) {
    fbl::AutoLock lock(&g_engine_mutex);

    for (size_t i = 0; i < g_observers.size(); i++) {
        if (g_observers[i].event == event) {
            bool awaited = g_observers[i].awaiting_update_after_start;
            g_observers.erase(i);
            if (awaited) {
                notify_engine_all_observers_started_if_needed_locked();
            }
            return ZX_OK;
        }
    }
    return ZX_ERR_NOT_FOUND;
}

void trace_notify_observer_updated(zx_handle_t event) {
    fbl::AutoLock lock(&g_engine_mutex);

    for (auto& item : g_observers) {
        if (item.event == event) {
            if (item.awaiting_update_after_start) {
                item.awaiting_update_after_start = false;
                notify_engine_all_observers_started_if_needed_locked();
            }
            return;
        }
    }
}
