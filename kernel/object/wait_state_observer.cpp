// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/wait_state_observer.h>

#include <assert.h>

#include <kernel/event.h>

#include <object/handle.h>
#include <object/dispatcher.h>

#include <fbl/type_support.h>

WaitStateObserver::~WaitStateObserver() {
    DEBUG_ASSERT(!dispatcher_);
}

zx_status_t WaitStateObserver::Begin(Event* event,
                                     Handle* handle,
                                     zx_signals_t watched_signals) {
    canary_.Assert();
    DEBUG_ASSERT(!dispatcher_);

    event_ = event;
    handle_ = handle;
    watched_signals_ = watched_signals;
    dispatcher_ = handle->dispatcher();
    wakeup_reasons_ = 0u;

    auto status = dispatcher_->add_observer(this);
    if (status != ZX_OK) {
        dispatcher_.reset();
        return status;
    }
    return ZX_OK;
}

zx_signals_t WaitStateObserver::End() {
    canary_.Assert();
    DEBUG_ASSERT(dispatcher_);
    DEBUG_ASSERT(dispatcher_->has_state_tracker());

    dispatcher_->RemoveObserver(this);
    dispatcher_.reset();

    // Return the set of reasons that we may have been woken.  Basically, this
    // is set of satisfied bits which were ever set while we were waiting on the list.
    return wakeup_reasons_;
}

StateObserver::Flags WaitStateObserver::OnInitialize(zx_signals_t initial_state,
                                                     const StateObserver::CountInfo* cinfo) {
    canary_.Assert();

    // Record the initial state of the state tracker as our wakeup reason.  If
    // we are going to become immediately signaled, the reason is contained
    // somewhere in this initial state.
    wakeup_reasons_ = initial_state;

    if (initial_state & watched_signals_) {
        event_->Signal();
    }

    return 0;
}

StateObserver::Flags WaitStateObserver::OnStateChange(zx_signals_t new_state) {
    canary_.Assert();

    // If we are still on our StateTracker's list of observers, and the
    // StateTracker's state has changed, accumulate the reasons that we may have
    // woken up.  In particular any satisfied bits which have become set
    // while we were on the list may have been reasons to wake up.
    wakeup_reasons_ |= new_state;

    if (new_state & watched_signals_) {
        event_->Signal();
    }

    return 0;
}

StateObserver::Flags WaitStateObserver::OnCancel(const Handle* handle) {
    canary_.Assert();

    if (handle == handle_) {
        wakeup_reasons_ |= ZX_SIGNAL_HANDLE_CLOSED;
        event_->Signal(ZX_ERR_CANCELED);
        return kHandled;
    } else {
        return 0;
    }
}
