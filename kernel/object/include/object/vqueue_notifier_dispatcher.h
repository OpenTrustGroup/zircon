// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <object/dispatcher.h>
#include <zircon/types.h>

// GRT's vqueue notifier: the object through which a VMM learns that a guest
// kicked it. A guest kicks with the Nebula fastcall NOTIFY_VMLOG_SINK, naming
// a VM ID; each notifier of that VM ID then gets ZX_USER_SIGNAL_0.
class VqueueNotifierDispatcher final
    : public SoloDispatcher,
      public fbl::DoublyLinkedListable<VqueueNotifierDispatcher*> {
public:
    static zx_status_t Create(uint32_t kick_irq, int32_t vmid,
                              fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights);

    // Signals each notifier of |vmid|.
    static void Notify(int32_t vmid);

    ~VqueueNotifierDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_VQUEUE_NOTIFIER; }
    bool has_state_tracker() const final { return true; }
    zx_signals_t allowed_user_signals() const final { return ZX_USER_SIGNAL_ALL; }
    void on_zero_handles() final;

private:
    explicit VqueueNotifierDispatcher(int32_t vmid);

    fbl::Canary<fbl::magic("VQND")> canary_;
    const int32_t vmid_;
    bool registered_ = false;
};
