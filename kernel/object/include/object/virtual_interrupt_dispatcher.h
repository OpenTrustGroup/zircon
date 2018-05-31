// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>
#include <fbl/ref_ptr.h>
#include <object/interrupt_dispatcher.h>
#include <sys/types.h>

class VirtualInterruptDispatcher final : public InterruptDispatcher {
public:
    static zx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights,
                              uint32_t options);

    VirtualInterruptDispatcher(const InterruptDispatcher &) = delete;
    VirtualInterruptDispatcher& operator=(const InterruptDispatcher &) = delete;

protected:
    void MaskInterrupt() final;
    void UnmaskInterrupt() final;
    void UnregisterInterruptHandler() final;

private:
    VirtualInterruptDispatcher() = default;
};
