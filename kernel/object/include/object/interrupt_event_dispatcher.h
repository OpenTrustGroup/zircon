// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>
#include <fbl/canary.h>
#include <object/interrupt_dispatcher.h>
#include <sys/types.h>

class InterruptEventDispatcher final : public InterruptDispatcher {
public:
    static zx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights,
                              uint32_t vector,
                              uint32_t options);

    InterruptEventDispatcher(const InterruptDispatcher &) = delete;
    InterruptEventDispatcher& operator=(const InterruptDispatcher &) = delete;

protected:
    void MaskInterrupt() final;
    void UnmaskInterrupt() final;
    void UnregisterInterruptHandler() final;

private:
    explicit InterruptEventDispatcher(uint32_t vector)
        : vector_(vector) {}
    zx_status_t RegisterInterruptHandler();

    static void IrqHandler(void* ctx);

    const uint32_t vector_;

    fbl::Canary<fbl::magic("INED")> canary_;
};
